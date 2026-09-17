// octave_engine.cppm — GNU Octave CLI 子进程封装（ectave 的“壳引擎”）。
//
// 引擎查找顺序：内置 engines/octave（OCTAVE_HOME 树，scripts/build_engines.sh
// 生成；子进程里 setenv OCTAVE_HOME/LD_LIBRARY_PATH/FLEXIBLAS_* 后绝对路径
// execv）→ PATH octave-cli → PATH octave。ECTAVE_OCTAVE_HOME 可显式指定内置根。
//
// 模型：forkpty 在真 PTY 里跑交互式 `octave -q --no-window-system`。
// 为什么必须是 PTY 而不是管道：管道模式下 octave 进入批处理语义——任何
// 运行时 error() 或**语法错误**都会直接终止整个解释器（Octave 10 又移除了
// set_error_handler("return")，try/catch 也罩不住 parse error），而 REPL 的
// 日常就是打错命令。PTY 里 octave 认为自己是对着真人终端：报错回到提示符、
// Ctrl+C（写 0x03 字节）中断当前行，行为与手敲完全一致。
//
// 分块协议：交互式会话没有“命令结束”事件，每条命令后追加一行
// `disp('%<token>-<seq>%')`，读线程按行精确匹配哨兵。哨兵是 disp 的**结果**
// 行，独立成行；readline 回显的是 disp 语句本身（带提示符前缀），不会误配。
// stdin 有序 ⇒ 哨兵按序返回，天然支持命令排队。
//
// 回显处理：PTY 会回显我们写入的每个字符行（提示符 + 命令）。引导时把
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

#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>      // fcntl(O_NONBLOCK)（master 非阻塞 + poll 读循环）
#include <poll.h>
#include <pty.h>        // forkpty（octave 必须活在真 PTY 里：见下方说明）
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>

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
            state_ = static_cast<int>(EngineState::Failed);
            std::lock_guard<std::mutex> lock(eventsMutex_);
            queue_.push_back({.kind = EventKind::StartFailed, .failReason = "not-found"});
            core::platform::requestUiUpdate();
            return;
        }

        int master = -1;
        const pid_t pid = forkpty(&master, nullptr, nullptr, nullptr);
        if (pid < 0) {
            state_ = static_cast<int>(EngineState::Failed);
            std::lock_guard<std::mutex> lock(eventsMutex_);
            queue_.push_back({.kind = EventKind::StartFailed, .failReason = "forkpty"});
            core::platform::requestUiUpdate();
            return;
        }
        if (pid == 0) {
            // 子进程：stdio 已被 forkpty 接到 PTY slave，octave 直接进入
            // 交互模式（错误终止、Ctrl+C 恢复——与真人终端一致）。
            char* const args[] = {const_cast<char*>("octave-cli"), const_cast<char*>("-q"),
                                  const_cast<char*>("--no-window-system"), nullptr};
            if (!home.empty()) {
                // 内置引擎：先摆好环境再绝对路径 execv。fork 后子进程是单
                // 线程、马上就要 exec，setenv 安全；字符串须活到 execv 为止。
                ::setenv("OCTAVE_HOME", home.c_str(), 1);
                std::string ldValue = home + "/lib";
                if (const char* old = ::getenv("LD_LIBRARY_PATH"); old != nullptr && *old != '\0') {
                    ldValue += ':';
                    ldValue += old;
                }
                ::setenv("LD_LIBRARY_PATH", ldValue.c_str(), 1);
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
            } else {
                ::execvp(program.c_str(), args);
            }
            _exit(127);
        }

        masterFd_ = master;
        ::fcntl(masterFd_, F_SETFL, ::fcntl(masterFd_, F_GETFL) | O_NONBLOCK);
        pid_ = pid;
        exited_ = false;
        stopFlag_ = false;
        state_ = static_cast<int>(EngineState::Starting);
        token_ = makeToken();

        // 宽终端：给 octave 自己的矩阵排版/换行留足列数，哨兵行（短）绝不
        // 会被终端宽度劈成两行。
        winsize ws{};
        ws.ws_row = 200;
        ws.ws_col = 500;
        ::ioctl(masterFd_, TIOCSWINSZ, &ws);

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
        if (pid_.load() <= 0 || exited_.load() || masterFd_ < 0) {
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

    // 中断当前计算：PTY 会话里写一个 Ctrl+C 字节（0x03），与真人按 Ctrl+C
    // 完全等价——octave 放弃当前输入行、回到提示符，进程不退出。
    void interrupt() {
        if (masterFd_ < 0) {
            return;
        }
        std::lock_guard<std::mutex> writeLock(writeMutex_);
        const char intr = '\x03';
        ssize_t n = ::write(masterFd_, &intr, 1);
        (void)n;
    }

    // 停会话：置停止标志让读线程退出（poll 超时 ≤100ms，绝不从别处 close 它
    // 正在 read 的 fd），join 之后由本线程关 master，SIGHUP/SIGKILL 兜底，
    // 最后回收。有界阻塞 ~1.5s。
    void stop() {
        if (pid_.load() <= 0) {
            return;
        }
        busy_ = false;
        stopFlag_ = true;
        if (reader_.joinable()) {
            reader_.join();
        }
        if (masterFd_ >= 0) {
            ::close(masterFd_);
            masterFd_ = -1;
        }
        const pid_t pid = pid_.load();
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
    static bool findOnPath(const char* program) {
        const std::string defaultDirs = ":/usr/local/bin:/usr/bin:/bin";
        std::string path = program;
        if (const char* env = ::getenv("PATH")) {
            path = env;
        }
        path += defaultDirs;
        std::size_t start = 0;
        while (start <= path.size()) {
            const auto sep = path.find(':', start);
            const std::string dir = path.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
            if (!dir.empty()) {
                const std::filesystem::path candidate = std::filesystem::path(dir) / program;
                std::error_code ec;
                const auto status = std::filesystem::status(candidate, ec);
                if (!ec && std::filesystem::is_regular_file(status) &&
                    (status.permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
                    return true;
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
    static std::string findBundledOctaveHome() {
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
        // 注意：run.sh 经系统 ld.so 显式加载（mcpp 私有 glibc 兼容问题），
        // 此时 /proc/self/exe 是 ld.so 而不是 ectave——所以还要看
        // /proc/self/cmdline 的第 0 个参数，最后兜底 cwd。
        auto addFromPathArg = [&](const std::string& arg0) {
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
    }

    std::string makeToken() const {
        // 随机哨兵基串：防止用户输出恰好撞上行哨兵。
        std::random_device rd;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04x%02x", rd() & 0xFFFFu,
                      static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count() & 0xFF));
        return buf;
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
                leftover.append(buf, static_cast<std::size_t>(n));
                for (;;) {
                    const auto nl = leftover.find('\n');
                    if (nl == std::string::npos) {
                        break;
                    }
                    const std::string_view line(leftover.data(), nl);
                    handleLine(line);
                    leftover.erase(0, nl + 1);
                }
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            }
            break;  // EOF / EIO：子进程已退出（PTY slave 关闭后 master 读返回 EIO）
        }
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
        // 注意：不回收 pid_（那是 stop()/start() 的职责，读线程抢先清零会
        // 漏掉 waitpid 回收、也会让 start() 误判为「仍在运行」）。
        exited_ = true;
        if (state_ != static_cast<int>(EngineState::Stopped)) {
            state_ = static_cast<int>(EngineState::Stopped);
        }
        core::platform::requestUiUpdate();
    }

    std::atomic<int> state_{static_cast<int>(EngineState::Stopped)};
    std::atomic<pid_t> pid_{-1};
    std::atomic<bool> busy_{false};
    std::atomic<bool> exited_{false};   // 读线程已见 EOF；start() 据此决定是否收尾重启
    std::atomic<bool> stopFlag_{false}; // stop() 请求读线程退出（先退线程再关 fd）
    int masterFd_ = -1;  // PTY master：UI 线程写（writeMutex_），读线程读（poll）
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
