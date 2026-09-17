// octave_engine.cppm — GNU Octave CLI 子进程封装（ectave 的“壳引擎”）。
//
// 引擎查找顺序：内置 engines/octave（OCTAVE_HOME 树，scripts/build_engines.sh
// 生成；子进程里 setenv OCTAVE_HOME/LD_LIBRARY_PATH/FLEXIBLAS_* 后绝对路径
// execv）→ PATH octave-cli → PATH octave。ECTAVE_OCTAVE_HOME 可显式指定内置根。
// 内置树只在 Linux/macOS 有意义：打包脚本是 Linux 的 ldd 闭包脚本，Windows
// 直接用 PATH（装官方 Octave 安装包即可，安装器会把 bin 加进 PATH）。
//
// 模型：POSIX 用 forkpty 造真 PTY，Windows 用 ConPTY（CreatePseudoConsole）——
// 两者是同一个东西（让 octave 认为自己对着真人终端），都跑交互式
// `octave -q --no-window-system`。
// 为什么必须是 PTY 而不是管道：管道模式下 octave 进入批处理语义——任何
// 运行时 error() 或**语法错误**都会直接终止整个解释器（Octave 10 又移除了
// set_error_handler("return")，try/catch 也罩不住 parse error），而 REPL 的
// 日常就是打错命令。PTY/ConPTY 里 octave 报错回到提示符、Ctrl+C（写 0x03
// 字节）中断当前行，行为与手敲完全一致。
//
// 平台差异只在进程/IO 层，协议与线程模型两平台共用：
//   - POSIX：master fd 非阻塞 + poll(100ms) 读循环；停止 = 置停止标志让读线程
//     先退出（绝不从别处关它正在读的 fd），再 close → SIGHUP → SIGKILL 兜底回收。
//   - Windows：ConPTY 的输入/输出各是一根匿名管道；读端用 PeekNamedPipe 轮询
//     （匿名管道不支持 overlapped I/O，阻塞读又没法响应停止），进程句柄用
//     WaitForSingleObject(0) 判活；停止 = TerminateProcess → 管道断开 → 读线程
//     自然退出 → 关伪控制台与句柄。CreatePseudoConsole/ResizePseudoConsole/
//     ClosePseudoConsole 三个入口从 kernel32 动态取：老 SDK 头里没有声明也能
//     编译，老系统（< Win10 1809）上运行期优雅失败（conpty-unavailable）。
//
// 分块协议：交互式会话没有“命令结束”事件，每条命令后追加一行
// `disp('%<token>-<seq>%')`，读线程按行精确匹配哨兵。哨兵是 disp 的**结果**
// 行，独立成行；readline 回显的是 disp 语句本身（带提示符前缀），不会误配。
// stdin 有序 ⇒ 哨兵按序返回，天然支持命令排队。
//
// 回显处理：PTY/ConPTY 会回显我们写入的每个字符行（提示符 + 命令）。引导时把
// PS1/PS2 设成固定前缀（kEchoPrefix/kEchoPrefix2），读线程据此丢弃回显行
// （控制台由 UI 自己打印「» 命令」回显，且着色/排版更好）。残余风险：用户
// 命令若恰好 disp 出以该前缀开头的行会被一起丢掉——固定前缀足够短小罕见，
// 换终端显示完整性，值得。
//
// 线程纪律：
//   - UI 线程：start()/send()/interrupt()/stop()/restart()/drainEvents()。
//   - 读线程：handleLine() 只在 eventsMutex_ 内写 queue_，随后
//     core::platform::requestUiUpdate() 唤醒 UI 一帧（跨线程安全）。
//   - restart() 的 stop() 会 join 读线程（最多 ~1.5s 宽限期），由调用方放
//     后台线程执行，UI 不空转等待。
//
// 工作区快照：requestWorkspace() 发送一行 for 循环（who + evalin('base')），
// 变量行以 "ECTVAR|name|class|dims" 前缀标记，UI 侧在 End(Workspace) 时提交，
// 不进控制台。变量名/类名/尺寸不含 '|'，直接切分即可。
module;

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // 否则 windows.h 的 min/max 宏会砸到 std::min/std::max
#endif
// STARTUPINFOEX / InitializeProcThreadAttributeList / PeekNamedPipe 的声明要有
// Vista+ 的头可见性；不显式定版本时老 SDK 会把它们藏起来（ConPTY 那几个入口
// 本来就走 GetProcAddress 动态取，但属性表这一套是直接调用的）。
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <csignal>
#include <fcntl.h>      // fcntl(O_NONBLOCK)（master 非阻塞 + poll 读循环）
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>  // _NSGetExecutablePath（内置引擎查找）
#include <util.h>         // macOS 的 forkpty 在 <util.h>（glibc 在 <pty.h>）
#else
#include <pty.h>          // forkpty（octave 必须活在真 PTY 里：见上方说明）
#endif
#endif
#include <cstdlib>
#include <cerrno>

// PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 需要 Win10 1809+ 的 SDK 头；老头上没有
// 宏定义也不该拦住编译（运行期本来就会优雅失败），这里补上官方定义（22）。
#if defined(_WIN32) && !defined(PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE)
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)
#endif

// eui 的 UI 唤醒（跨模块前向声明放全局模块片段尾部，避免模块名改锁；
// 与 tinynext/cli.cppm 同一写法）。
namespace core::platform { void requestUiUpdate(); }

export module ectave.octave_engine;

import std;
import ectave.utils;

export namespace ectave {

// 交互式提示符前缀（引导里设死，回显行据此丢弃）。echo-on 下 readline 回显
// 独占「提示符+命令」整行、程序输出行独立干净；若 stty -e 关掉内核 echo，
// 待输出的提示符会粘进下一条结果行行首、破坏哨兵精确匹配——保持 echo on，
// 整行丢弃回显即可。
inline constexpr std::string_view kEchoPrefix = "ec> ";
inline constexpr std::string_view kEchoPrefix2 = "ec2> ";

// 伪终端尺寸：给 octave 自己的矩阵排版/换行留足列数，哨兵行（短）绝不
// 会被终端宽度劈成两行。
inline constexpr int kTermRows = 200;
inline constexpr int kTermCols = 500;

enum class EngineState { Stopped, Starting, Ready, Failed };
enum class SeqPurpose { Bootstrap, Command, Workspace };
enum class EventKind { Output, Var, Boot, End, Exited, StartFailed };

struct OctVar {
    std::string name;
    std::string cls;
    std::string dims;
};

struct EngineEvent {
    EventKind kind{EventKind::Output};
    std::string text;                          // Output 行 / Boot 版本号
    OctVar var;                                // Var
    SeqPurpose purpose{SeqPurpose::Command};   // End
    std::string failReason;                    // StartFailed
};

class OctaveEngine {
public:
    OctaveEngine() = default;
    ~OctaveEngine() { stop(); }
    OctaveEngine(const OctaveEngine&) = delete;
    OctaveEngine& operator=(const OctaveEngine&) = delete;

    // 全局唯一实例（app.cpp 启动，store/UI 直接引用）。
    static OctaveEngine& instance() {
        static OctaveEngine engine;
        return engine;
    }

    EngineState state() const { return static_cast<EngineState>(state_.load()); }
    bool running() const { return pid_.load() > 0; }
    bool busy() const { return busy_.load(); }

    // 启动 octave 子进程并发送自检引导（more off + 版本上报）。找不到可执行
    // 文件时发 StartFailed 事件，不抛异常。
    void start() {
        if (pid_.load() > 0) {
            // 读线程已检测到退出（exited_）：先收尾回收，再重新拉起；否则视为
            // 仍在运行，直接返回。
            if (!exited_.load()) {
                return;
            }
            stop();
        }
        // 引擎优先级：内置 engines/octave（OCTAVE_HOME 树，见
        // scripts/build_engines.sh）→ PATH 上的 octave-cli → octave。
        const std::string home = findBundledOctaveHome();
        std::string program;
        if (!home.empty()) {
            program = home + "/bin/octave-cli";
        } else if (findOnPath("octave-cli")) {
            program = "octave-cli";
        } else if (findOnPath("octave")) {
            program = "octave";
        }
        if (program.empty()) {
            failStart("not-found");
            return;
        }

#if defined(_WIN32)
        const EnginePid pid = spawnConpty(program);
        if (pid <= 0) {
            failStart(spawnFailReason_.empty() ? "spawn" : spawnFailReason_);
            return;
        }
        pid_ = pid;
#else
        int master = -1;
        const pid_t pid = forkpty(&master, nullptr, nullptr, nullptr);
        if (pid < 0) {
            failStart("forkpty");
            return;
        }
        if (pid == 0) {
            // 子进程：stdio 已被 forkpty 接到 PTY slave，octave 直接进入
            // 交互模式（错误终止、Ctrl+C 恢复——与真人终端一致）。
            execChild(home, program);
            _exit(127);  // execChild 只在失败时返回
        }
        masterFd_ = master;
        ::fcntl(masterFd_, F_SETFL, ::fcntl(masterFd_, F_GETFL) | O_NONBLOCK);
        pid_ = static_cast<EnginePid>(pid);

        winsize ws{};
        ws.ws_row = static_cast<unsigned short>(kTermRows);
        ws.ws_col = static_cast<unsigned short>(kTermCols);
        ::ioctl(masterFd_, TIOCSWINSZ, &ws);
#endif

        exited_ = false;
        stopFlag_ = false;
        state_ = static_cast<int>(EngineState::Starting);
        token_ = makeToken();

        // 引导：关分页（tty 下 "-- more --" 会挂起输入流）、固定 PS1/PS2
        // （回显按前缀丢弃）、上报版本。
        send("more off;\n"
             "PS1 = \"ec> \";\n"
             "PS2 = \"ec2> \";\n"
             "printf(\"ECTAVE_BOOT|%s\\n\", OCTAVE_VERSION());",
             SeqPurpose::Bootstrap);

        reader_ = std::thread([this] { readLoop(); });
    }

    // 发送一段命令（可多行），末尾自动追加本命令的哨行。返回 false = 进程未运行。
    bool send(const std::string& command, SeqPurpose purpose) {
        if (pid_.load() <= 0 || exited_.load() || !ioReady()) {
            return false;
        }
        std::uint64_t seq = 0;
        {
            std::lock_guard<std::mutex> lock(eventsMutex_);
            seq = ++seq_;
            purposes_[seq] = purpose;
            pendingSeq_.push_back(seq);
            busy_ = true;
        }
        std::string payload = command;
        payload += "\ndisp('%";
        payload += token_;
        payload += '-';
        payload += std::to_string(seq);
        payload += "%');\n";
        std::lock_guard<std::mutex> writeLock(writeMutex_);
        writeAll(payload);
        return true;
    }

    // 中断当前计算：往终端里写一个 Ctrl+C 字节（0x03），与真人按 Ctrl+C
    // 完全等价——octave 放弃当前输入行、回到提示符，进程不退出。
    void interrupt() {
        if (!ioReady()) {
            return;
        }
        std::lock_guard<std::mutex> writeLock(writeMutex_);
        writeCtrlC();
    }

    // 停会话：置停止标志让读线程退出（POSIX 侧 poll 超时 ≤100ms，绝不从别处
    // close 它正在 read 的 fd；Windows 侧先杀子进程让管道断开），join 之后由
    // 本线程关句柄/回收。有界阻塞 ~1.5s。
    void stop() {
        if (pid_.load() <= 0) {
            return;
        }
        busy_ = false;
        stopFlag_ = true;
#if defined(_WIN32)
        // 先结束子进程：管道写端随进程与伪控制台关闭，读线程的 PeekNamedPipe
        // 立刻能判出「进程没了」而退出。
        if (proc_ != nullptr) {
            ::TerminateProcess(proc_, 0);
            ::WaitForSingleObject(proc_, 2000);
        }
        if (reader_.joinable()) {
            reader_.join();
        }
        closeConpty();
#else
        if (reader_.joinable()) {
            reader_.join();
        }
        if (masterFd_ >= 0) {
            ::close(masterFd_);
            masterFd_ = -1;
        }
        const pid_t pid = static_cast<pid_t>(pid_.load());
        bool reaped = false;
        for (int i = 0; i < 30 && pid > 0; ++i) {
            if (::waitpid(pid, nullptr, WNOHANG) == pid) {
                reaped = true;
                break;
            }
            if (i == 10) {
                ::kill(pid, SIGHUP);
            }
            ::usleep(50'000);
        }
        if (!reaped && pid > 0) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
        }
#endif
        pid_ = -1;
        exited_ = false;
        stopFlag_ = false;
        {
            std::lock_guard<std::mutex> lock(eventsMutex_);
            pendingSeq_.clear();
            purposes_.clear();
        }
        state_ = static_cast<int>(EngineState::Stopped);
    }

    void restart() {
        stop();
        start();
    }

    // UI 线程每帧取走后台事件。
    std::deque<EngineEvent> drainEvents() {
        std::deque<EngineEvent> out;
        std::lock_guard<std::mutex> lock(eventsMutex_);
        out.swap(queue_);
        return out;
    }

private:
#if defined(_WIN32)
    using EnginePid = long long;   // Windows 的进程 id（DWORD）放宽存
#else
    using EnginePid = pid_t;
#endif

    void failStart(const std::string& reason) {
        state_ = static_cast<int>(EngineState::Failed);
        std::lock_guard<std::mutex> lock(eventsMutex_);
        queue_.push_back({.kind = EventKind::StartFailed, .failReason = reason});
        core::platform::requestUiUpdate();
    }

    bool ioReady() const {
#if defined(_WIN32)
        return ptyIn_ != nullptr && ptyOut_ != nullptr;
#else
        return masterFd_ >= 0;
#endif
    }

    // PATH 查找：Windows 用 ';' 分隔并试 PATHEXT 风格后缀，POSIX 用 ':' 且要求
    // 可执行位。两侧都补一串默认目录，避免 PATH 被裁剪时漏掉常规位置
    // （/opt/homebrew/bin 是 Apple Silicon 的 brew 前缀）。
    static bool findOnPath(const std::string& program) {
#if defined(_WIN32)
        constexpr char kSep = ';';
        const std::string defaultDirs;
        const std::vector<std::string> suffixes = {"", ".exe", ".cmd", ".bat", ".com"};
#else
        constexpr char kSep = ':';
        const std::string defaultDirs = ":/usr/local/bin:/opt/homebrew/bin:/usr/bin:/bin";
        const std::vector<std::string> suffixes = {""};
#endif
        std::string path;
        if (const char* env = ::getenv("PATH")) {
            path = env;
        }
        path += defaultDirs;
        std::size_t start = 0;
        while (start <= path.size()) {
            const auto sep = path.find(kSep, start);
            const std::string dir = path.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
            if (!dir.empty()) {
                for (const auto& suffix : suffixes) {
                    const std::filesystem::path candidate = std::filesystem::path(dir) / (program + suffix);
                    std::error_code ec;
                    const auto status = std::filesystem::status(candidate, ec);
                    if (ec || !std::filesystem::is_regular_file(status)) {
                        continue;
                    }
#if defined(_WIN32)
                    return true;   // Windows 没有可执行位
#else
                    if ((status.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
                        return true;
                    }
#endif
                }
            }
            if (sep == std::string::npos) {
                break;
            }
            start = sep + 1;
        }
        return false;
    }

    // 内置引擎目录（OCTAVE_HOME 树：bin/octave-cli、lib/、share/octave/、
    // lib64/octave/）。由 scripts/build_engines.sh 生成。开发期 exe 在
    // target/<triple>/<cfg>/bin/ 下，engines/ 在项目根——从 exe 目录逐级向上找；
    // ECTAVE_OCTAVE_HOME 环境变量可显式指定（打包发布时引擎放 exe 同级）。
    // Windows 直接返回空：打包脚本是 Linux 的 ldd 闭包脚本，那边走 PATH。
    static std::string findBundledOctaveHome() {
#if defined(_WIN32)
        return {};
#else
        const auto usable = [](const std::filesystem::path& p) {
            std::error_code ec;
            const auto st = std::filesystem::status(p, ec);
            return !ec && std::filesystem::is_regular_file(st) &&
                   (st.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
        };
        std::vector<std::filesystem::path> roots;
        if (const char* env = ::getenv("ECTAVE_OCTAVE_HOME"); env != nullptr && *env != '\0') {
            roots.emplace_back(env);
        }
        // 从 dir 起逐级向上收集 <dir>/engines/octave 候选（mcpp 产物在
        // target/<triple>/<fp>/bin/ —— 到项目根要上溯 4 级，多留几级余量）。
        const auto addUpwards = [&roots](std::filesystem::path dir) {
            for (int up = 0; up <= 6; ++up) {
                roots.push_back(dir / "engines" / "octave");
                const auto parent = dir.parent_path();
                if (parent.empty() || parent == dir) {
                    break;
                }
                dir = parent;
            }
        };
        const auto addFromPathArg = [&addUpwards](const std::string& arg0) {
            if (arg0.empty()) {
                return;
            }
            std::error_code ec;
            std::filesystem::path p = std::filesystem::weakly_canonical(arg0, ec);
            if (ec || p.empty()) {
                p = std::filesystem::path(arg0);
            }
            addUpwards(p.parent_path());
        };
#if defined(__linux__)
        // 注意：run.sh 经系统 ld.so 显式加载（mcpp 私有 glibc 兼容问题），
        // 此时 /proc/self/exe 是 ld.so 而不是 ectave——所以还要看
        // /proc/self/cmdline 的第 0 个参数，最后兜底 cwd。
        {
            std::ifstream cmd("/proc/self/cmdline", std::ios::binary);
            std::string arg0;
            std::getline(cmd, arg0, '\0');
            addFromPathArg(arg0);
        }
        {
            char buf[4096];
            const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                addFromPathArg(buf);
            }
        }
#else
        {
            char buf[4096];
            std::uint32_t size = sizeof(buf);
            if (_NSGetExecutablePath(buf, &size) == 0) {
                addFromPathArg(buf);
            }
        }
#endif
        {
            std::error_code ec;
            const auto cwd = std::filesystem::current_path(ec);
            if (!ec) {
                addUpwards(cwd);
            }
        }
        for (const auto& root : roots) {
            if (usable(root / "bin" / "octave-cli")) {
                return root.string();
            }
        }
        return {};
#endif
    }

    std::string makeToken() const {
        // 随机哨兵基串：防止用户输出恰好撞上行哨兵。
        std::random_device rd;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04x%02x", rd() & 0xFFFFu,
                      static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count() & 0xFF));
        return buf;
    }

#if !defined(_WIN32)
    // 子进程侧：内置引擎先摆好环境再绝对路径 execv。fork 后子进程是单线程、
    // 马上就要 exec，setenv 安全；字符串须活到 execv 为止。
    static void execChild(const std::string& home, const std::string& program) {
        char* const args[] = {const_cast<char*>("octave-cli"), const_cast<char*>("-q"),
                              const_cast<char*>("--no-window-system"), nullptr};
        if (home.empty()) {
            ::execvp(program.c_str(), args);
            return;
        }
        ::setenv("OCTAVE_HOME", home.c_str(), 1);
        const auto prependEnv = [&home](const char* name, const std::string& value) {
            std::string combined = value;
            if (const char* old = ::getenv(name); old != nullptr && *old != '\0') {
                combined += ':';
                combined += old;
            }
            ::setenv(name, combined.c_str(), 1);
        };
        prependEnv("LD_LIBRARY_PATH", home + "/lib");
#if defined(__APPLE__)
        prependEnv("DYLD_LIBRARY_PATH", home + "/lib");  // dyld 只认 DYLD_*，不认 LD_*
#endif
        // flexiblas 的 provider/后端是运行时 dlopen，不在 ldd 闭包里，
        // 打包时单独收集——只在目录存在时才设，别把 PATH 模式的机器带偏。
        std::error_code ec;
        const std::string flexLib = home + "/lib/flexiblas";
        if (std::filesystem::is_directory(flexLib, ec)) {
            ::setenv("FLEXIBLAS_LIBRARY_PATH", flexLib.c_str(), 1);
            const std::string flexRc = home + "/etc/flexiblasrc";
            if (std::filesystem::exists(flexRc, ec)) {
                ::setenv("FLEXIBLAS_CONFIG", flexRc.c_str(), 1);
            }
        }
        ::execv(program.c_str(), args);
    }

    void writeAll(const std::string& payload) {
        // masterFd_ 非阻塞：EAGAIN 时短 poll POLLOUT 重试（payload 很小，
        // 实际上 tty 缓冲一次就吞下）。子进程已死 → 放弃，等读线程报 Exited。
        std::size_t done = 0;
        while (done < payload.size()) {
            const ssize_t n = ::write(masterFd_, payload.data() + done, payload.size() - done);
            if (n > 0) {
                done += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
                pollfd waiter{masterFd_, POLLOUT, 0};
                ::poll(&waiter, 1, 100);
                continue;
            }
            return;
        }
    }

    void writeCtrlC() {
        const char intr = '\x03';
        const ssize_t n = ::write(masterFd_, &intr, 1);
        (void)n;
    }

    void readLoop() {
        std::string leftover;
        char buf[8192];
        // stopFlag_ 由 stop() 置位：先退读线程、后关 master，绝不出现
        // 「正 read 的 fd 被别处 close」的竞争；poll 100ms 一醒保证及时响应。
        while (!stopFlag_.load()) {
            pollfd pfd{masterFd_, POLLIN, 0};
            const int ready = ::poll(&pfd, 1, 100);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (ready == 0) {
                continue;
            }
            const ssize_t n = ::read(masterFd_, buf, sizeof(buf));
            if (n > 0) {
                feed(leftover, buf, static_cast<std::size_t>(n));
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            }
            break;  // EOF / EIO：子进程已退出（PTY slave 关闭后 master 读返回 EIO）
        }
        endReadLoop(std::move(leftover));
    }
#else  // _WIN32 —— ConPTY
    // kernel32 动态入口（老 SDK 头/老系统都能编过、跑不动时优雅失败）。
    static void* conptyEntry(const char* name) {
        return reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), name));
    }

    // 造伪控制台 + 挂子进程，返回子进程 id（失败返回 -1，spawnFailReason_ 带原因）。
    EnginePid spawnConpty(const std::string& program) {
        using CreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, void**);
        const auto createPseudoConsole =
            reinterpret_cast<CreatePseudoConsoleFn>(conptyEntry("CreatePseudoConsole"));
        if (createPseudoConsole == nullptr) {
            spawnFailReason_ = "conpty-unavailable";  // Win10 1809 以下
            return -1;
        }
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
        if (!::CreatePipe(&inRead, &inWrite, &sa, 0)) {
            spawnFailReason_ = "pipe";
            return -1;
        }
        if (!::CreatePipe(&outRead, &outWrite, &sa, 0)) {
            ::CloseHandle(inRead);
            ::CloseHandle(inWrite);
            spawnFailReason_ = "pipe";
            return -1;
        }
        COORD size{static_cast<SHORT>(kTermCols), static_cast<SHORT>(kTermRows)};
        void* pcon = nullptr;
        if (FAILED(createPseudoConsole(size, inRead, outWrite, 0, &pcon))) {
            ::CloseHandle(inRead);
            ::CloseHandle(inWrite);
            ::CloseHandle(outRead);
            ::CloseHandle(outWrite);
            spawnFailReason_ = "conpty-create";
            return -1;
        }
        // 伪控制台自己持有这两端（内部已复制句柄），我们手里的副本要关掉，
        // 否则读端永远等不到 EOF。
        ::CloseHandle(inRead);
        ::CloseHandle(outWrite);

        SIZE_T attrSize = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
        std::vector<char> attrBuf(attrSize);
        auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
        if (!::InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)) {
            destroyConpty(pcon, inWrite, outRead);
            spawnFailReason_ = "attr-list";
            return -1;
        }
        const BOOL attrOk = ::UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pcon,
                                                        sizeof(pcon), nullptr, nullptr);
        STARTUPINFOEXA si{};
        si.StartupInfo.cb = sizeof(si);
        si.lpAttributeList = attrs;
        // lpApplicationName 传 nullptr：让 CreateProcess 自己按 PATH + .exe 找，
        // == POSIX 侧 execvp 的语义。
        std::string cmdline = "\"" + program + "\" -q --no-window-system";
        PROCESS_INFORMATION pi{};
        const BOOL launched =
            attrOk && ::CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                                       EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                       &si.StartupInfo, &pi);
        ::DeleteProcThreadAttributeList(attrs);
        if (!launched) {
            destroyConpty(pcon, inWrite, outRead);
            spawnFailReason_ = "spawn";
            return -1;
        }
        ::CloseHandle(pi.hThread);
        proc_ = pi.hProcess;
        pcon_ = pcon;
        ptyIn_ = inWrite;
        ptyOut_ = outRead;
        return static_cast<EnginePid>(pi.dwProcessId);
    }

    static void closePcon(void* pcon) {
        using ClosePseudoConsoleFn = void(WINAPI*)(void*);
        const auto closePseudoConsole =
            reinterpret_cast<ClosePseudoConsoleFn>(conptyEntry("ClosePseudoConsole"));
        if (closePseudoConsole != nullptr) {
            closePseudoConsole(pcon);
        }
    }

    static void destroyConpty(void* pcon, HANDLE inWrite, HANDLE outRead) {
        if (pcon != nullptr) {
            closePcon(pcon);
        }
        if (inWrite != nullptr) {
            ::CloseHandle(inWrite);
        }
        if (outRead != nullptr) {
            ::CloseHandle(outRead);
        }
    }

    void closeConpty() {
        if (pcon_ != nullptr) {
            closePcon(pcon_);
            pcon_ = nullptr;
        }
        if (ptyIn_ != nullptr) {
            ::CloseHandle(ptyIn_);
            ptyIn_ = nullptr;
        }
        if (ptyOut_ != nullptr) {
            ::CloseHandle(ptyOut_);
            ptyOut_ = nullptr;
        }
        if (proc_ != nullptr) {
            ::CloseHandle(proc_);
            proc_ = nullptr;
        }
    }

    void writeAll(const std::string& payload) {
        // ConPTY 输入管道是阻塞写：payload 很小（一条命令 + 哨兵行），缓冲区
        // 一次吞下；子进程已死时 WriteFile 直接失败返回，交给读线程报 Exited。
        std::size_t done = 0;
        while (done < payload.size()) {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(payload.size() - done, 1u << 20));
            if (!::WriteFile(ptyIn_, payload.data() + done, chunk, &written, nullptr) || written == 0) {
                return;
            }
            done += written;
        }
    }

    void writeCtrlC() {
        // 输入管道里写 0x03：conhost 把它当成 Ctrl+C 键事件交给子进程，等价于
        // 真人在终端里按 Ctrl+C（octave 放弃当前行回到提示符）。
        const char intr = '\x03';
        DWORD written = 0;
        ::WriteFile(ptyIn_, &intr, 1, &written, nullptr);
    }

    // 非阻塞取一次可用数据；返回是否有数据被读走。
    bool pumpOnce(char* buf, std::size_t cap, std::string& leftover) {
        if (ptyOut_ == nullptr) {
            return false;
        }
        DWORD avail = 0;
        if (!::PeekNamedPipe(ptyOut_, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) {
            return false;
        }
        DWORD got = 0;
        if (!::ReadFile(ptyOut_, buf, static_cast<DWORD>(cap), &got, nullptr) || got == 0) {
            return false;
        }
        feed(leftover, buf, static_cast<std::size_t>(got));
        return true;
    }

    void readLoop() {
        std::string leftover;
        char buf[8192];
        bool childGone = false;
        while (!stopFlag_.load()) {
            bool progressed = false;
            while (pumpOnce(buf, sizeof(buf), leftover)) {
                progressed = true;
            }
            if (proc_ != nullptr && ::WaitForSingleObject(proc_, 0) == WAIT_OBJECT_0) {
                childGone = true;
                break;
            }
            if (!progressed) {
                ::Sleep(50);  // 等价 POSIX 侧的 poll(100ms) 等待
            }
        }
        if (childGone) {
            // 子进程退了但伪控制台可能还有没刷出来的输出，短促排空几次再收尾。
            for (int i = 0; i < 20; ++i) {
                if (pumpOnce(buf, sizeof(buf), leftover)) {
                    continue;
                }
                ::Sleep(10);
            }
        }
        endReadLoop(std::move(leftover));
    }
#endif

    // 把新读到的字节并进残留缓冲，按 \n 切行喂给 handleLine（两平台共用）。
    void feed(std::string& leftover, const char* data, std::size_t n) {
        leftover.append(data, n);
        for (;;) {
            const auto nl = leftover.find('\n');
            if (nl == std::string::npos) {
                break;
            }
            const std::string_view line(leftover.data(), nl);
            handleLine(line);
            leftover.erase(0, nl + 1);
        }
    }

    // 读线程收尾（两平台共用）：残余半行、Exited 事件、状态落定。
    void endReadLoop(std::string leftover) {
        if (!leftover.empty()) {
            handleLine(leftover);
        }
        {
            std::lock_guard<std::mutex> lock(eventsMutex_);
            pendingSeq_.clear();
            purposes_.clear();
            busy_ = false;
            queue_.push_back({.kind = EventKind::Exited});
        }
        // 注意：不回收 pid_/句柄（那是 stop()/start() 的职责，读线程抢先清零会
        // 漏掉 waitpid/CloseHandle、也会让 start() 误判为「仍在运行」）。
        exited_ = true;
        if (state_ != static_cast<int>(EngineState::Stopped)) {
            state_ = static_cast<int>(EngineState::Stopped);
        }
        core::platform::requestUiUpdate();
    }

    void handleLine(std::string_view lineView) {
        // 内核 echo 把「提示符+命令」原样打回，整行丢弃——控制台回显由 UI
        // 自己渲染（见文件头「回显处理」）。必须在锁外做，纯字符串判断。
        if (startsWith(lineView, kEchoPrefix) || startsWith(lineView, kEchoPrefix2)) {
            return;
        }
        std::string line(lineView);
        std::lock_guard<std::mutex> lock(eventsMutex_);
        // 哨兵行：%%<token>-<seq>%%，整行精确匹配才算（用户 echo 输出带不进 %%）。
        const std::string sentinelHead = "%" + token_ + "-";
        if (startsWith(line, sentinelHead) && line.size() > sentinelHead.size() + 2 && line.back() == '%') {
            const std::string seqText = line.substr(sentinelHead.size(), line.size() - sentinelHead.size() - 2);
            // 只认队首 seq（哨兵按 stdin 写入顺序返回）；不匹配的行当作普通输出。
            if (!pendingSeq_.empty()) {
                const std::string head = std::to_string(pendingSeq_.front());
                if (seqText == head) {
                    const std::uint64_t seq = pendingSeq_.front();
                    pendingSeq_.pop_front();
                    SeqPurpose purpose = SeqPurpose::Command;
                    if (const auto it = purposes_.find(seq); it != purposes_.end()) {
                        purpose = it->second;
                        purposes_.erase(it);
                    }
                    busy_ = !pendingSeq_.empty();
                    queue_.push_back({.kind = EventKind::End, .purpose = purpose});
                    core::platform::requestUiUpdate();
                    return;
                }
            }
        } else if (startsWith(line, "ECTVAR|")) {
            // ECTVAR|name|class|dims（三段，dims 不含 '|'）
            const auto parts = splitPipe(line, 4);
            if (parts.size() == 4) {
                // 打印顺序为 name|class|dims → OctVar{name, cls, dims}
                queue_.push_back({.kind = EventKind::Var,
                                  .var = OctVar{parts[1], parts[2], parts[3]}});
                core::platform::requestUiUpdate();
                return;
            }
        } else if (startsWith(line, "ECTAVE_BOOT|")) {
            state_ = static_cast<int>(EngineState::Ready);
            queue_.push_back({.kind = EventKind::Boot, .text = std::string(line.substr(12))});
            core::platform::requestUiUpdate();
            return;
        }
        queue_.push_back({.kind = EventKind::Output, .text = std::move(line)});
        core::platform::requestUiUpdate();
    }

    static std::vector<std::string> splitPipe(std::string_view s, std::size_t limit) {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (out.size() + 1 < limit) {
            const auto sep = s.find('|', start);
            if (sep == std::string_view::npos) {
                break;
            }
            out.emplace_back(s.substr(start, sep - start));
            start = sep + 1;
        }
        out.emplace_back(s.substr(start));
        return out;
    }

    std::atomic<int> state_{static_cast<int>(EngineState::Stopped)};
    std::atomic<EnginePid> pid_{-1};
    std::atomic<bool> busy_{false};
    std::atomic<bool> exited_{false};   // 读线程已见 EOF；start() 据此决定是否收尾重启
    std::atomic<bool> stopFlag_{false}; // stop() 请求读线程退出（先退线程再关句柄）
#if defined(_WIN32)
    HANDLE ptyIn_ = nullptr;   // 伪控制台输入：UI 线程写（writeMutex_）
    HANDLE ptyOut_ = nullptr;  // 伪控制台输出：读线程 PeekNamedPipe 轮询
    void* pcon_ = nullptr;     // HPCON（CreatePseudoConsole 产物）
    HANDLE proc_ = nullptr;    // 子进程句柄
    std::string spawnFailReason_;
#else
    int masterFd_ = -1;  // PTY master：UI 线程写（writeMutex_），读线程读（poll）
#endif
    std::thread reader_;
    std::mutex writeMutex_;
    std::mutex eventsMutex_;
    std::deque<EngineEvent> queue_;
    std::deque<std::uint64_t> pendingSeq_;
    std::unordered_map<std::uint64_t, SeqPurpose> purposes_;
    std::uint64_t seq_ = 0;  // eventsMutex_ 保护
    std::string token_;
};

inline OctaveEngine& g_octave = OctaveEngine::instance();

} // namespace ectave
