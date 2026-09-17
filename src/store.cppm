// store.cppm — ectave 的会话 store：控制台逻辑行、命令历史、脚本缓冲、
// 工作区变量、视图位置与引擎事件同步。
//
// 线程纪律：本模块所有 g_* 只被 UI 线程读写；后台（引擎读线程）数据经
// OctaveEngine 自己的事件队列中转，UI 在 compose 开头调 syncFromEngine()。
export module ectave.store;

import std;
import ectave.octave_engine;
import ectave.utils;

export namespace ectave {

enum class LineKind { Prompt, Output, Error, Warning, Info, Dim };
enum class Pane { Console, Script };

struct ConsoleLine {
    LineKind kind{LineKind::Output};
    std::string text;  // 可含 \n（多行命令回显），软换行在 UI 侧做
};

constexpr std::size_t kMaxConsoleLines = 4000;

// ---- 控制台 ----

inline std::vector<ConsoleLine> g_console;
inline bool g_consoleDirty = true;        // 显示行需要重新软换行
inline bool g_consoleTruncated = false;   // 触发过截断（提示一次）

inline void appendLine(LineKind kind, std::string text) {
    g_console.push_back({kind, std::move(text)});
    if (g_console.size() > kMaxConsoleLines) {
        const std::size_t excess = g_console.size() - kMaxConsoleLines + 64;  // 成批丢，避免每行都 erase
        g_console.erase(g_console.begin(), g_console.begin() + static_cast<std::ptrdiff_t>(excess));
        g_consoleTruncated = true;
    }
    g_consoleDirty = true;
}

// octave 管道模式没有 ANSI 颜色；按行首前缀做朴素分类。
inline LineKind classifyOutputLine(const std::string& line) {
    const std::string_view head = [&] {
        std::size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        return std::string_view{line}.substr(i);
    }();
    if (startsWith(head, "error:") || startsWith(head, "parse error")) {
        return LineKind::Error;
    }
    if (startsWith(head, "warning:")) {
        return LineKind::Warning;
    }
    return LineKind::Output;
}

// ---- 命令输入 / 历史 ----

inline std::string g_commandText;
inline std::vector<std::string> g_history;
inline std::size_t g_historyIndex = static_cast<std::size_t>(-1);  // -1 = 新输入位
inline std::string g_draft;  // 翻历史时暂存的当前草稿

// dir: -1 更旧，+1 更新。仅在单行缓冲时由 command_input 调用。
inline void applyHistory(int dir) {
    if (g_history.empty()) {
        return;
    }
    if (dir < 0) {
        if (g_historyIndex == static_cast<std::size_t>(-1)) {
            g_draft = g_commandText;
            g_historyIndex = g_history.size();
        }
        if (g_historyIndex > 0) {
            --g_historyIndex;
        }
        g_commandText = g_history[g_historyIndex];
    } else {
        if (g_historyIndex == static_cast<std::size_t>(-1)) {
            return;
        }
        ++g_historyIndex;
        if (g_historyIndex >= g_history.size()) {
            g_historyIndex = static_cast<std::size_t>(-1);
            g_commandText = g_draft;
        } else {
            g_commandText = g_history[g_historyIndex];
        }
    }
}

// ---- 脚本页 ----

inline std::string g_scriptText =
    "// ectave 脚本 — 整个编辑区一次发给 Octave。\n"
    "// F5 或「运行脚本」执行；多行块写成 for i = 1:3, ..., end 或分行均可。\n";

// ---- 工作区 ----

inline std::vector<OctVar> g_vars;        // 当前展示快照
inline std::vector<OctVar> g_varsPending; // 本轮 ECTVAR 收集，End(Workspace) 提交
inline bool g_showWorkspace = true;

// ---- 视图状态 ----

inline Pane g_pane = Pane::Console;
inline float g_consoleOffset = 0.0f;
inline bool g_followOutput = true;   // 贴底跟随新输出；用户上滚后关闭

// ---- 会话信息 ----

inline std::string g_octaveVersion;
inline std::size_t g_outstandingCommands = 0;  // 在飞的 Command（不含工作区刷新）

// ---- 动作 ----

// 提交命令栏缓冲（可多行：Shift+Enter 拼出来的块整体发送）。
inline bool sendCommand(OctaveEngine& engine) {
    const std::string command = trim(g_commandText);
    if (command.empty()) {
        return false;
    }
    if (g_history.empty() || g_history.back() != command) {
        g_history.push_back(command);
    }
    g_historyIndex = static_cast<std::size_t>(-1);
    g_draft.clear();
    appendLine(LineKind::Prompt, command);
    if (!engine.send(command, SeqPurpose::Command)) {
        appendLine(LineKind::Info, "ectave: Octave 未运行 — 点工具栏「重启」拉起");
        return false;
    }
    ++g_outstandingCommands;
    g_commandText.clear();
    return true;
}

inline bool runScript(OctaveEngine& engine) {
    const std::string body = trim(g_scriptText);
    if (body.empty()) {
        return false;
    }
    g_pane = Pane::Console;
    appendLine(LineKind::Prompt, "% script\n" + body);
    if (!engine.send(body, SeqPurpose::Command)) {
        appendLine(LineKind::Info, "ectave: Octave 未运行 — 点工具栏「重启」拉起");
        return false;
    }
    ++g_outstandingCommands;
    return true;
}

// 工作区快照：一行 for 循环（语句用逗号连接，end 收尾），临时变量 __ect_*
// 不入库（名字过滤 + 用完 clear）。
inline void requestWorkspace(OctaveEngine& engine) {
    if (!engine.running()) {
        return;
    }
    engine.send(
        "__ect_w = who(); for __ect_i = 1:numel(__ect_w), __ect_n = __ect_w{__ect_i}; "
        "__ect_s = evalin('base', __ect_n); "
        "if ~strncmp(__ect_n, '__ect_', 6), "
        "printf('ECTVAR|%s|%s|%s\\n', __ect_n, class(__ect_s), mat2str(size(__ect_s))); end, end; "
        "clear __ect_w __ect_i __ect_n __ect_s;",
        SeqPurpose::Workspace);
}

inline void clearConsole() {
    g_console.clear();
    g_consoleTruncated = false;
    g_consoleDirty = true;
    g_followOutput = true;
}

// UI 线程每帧 compose 开头：排空引擎事件信箱 → 落到 store。
inline void syncFromEngine(OctaveEngine& engine) {
    for (auto& event : engine.drainEvents()) {
        switch (event.kind) {
        case EventKind::Output:
            appendLine(classifyOutputLine(event.text), event.text);
            break;
        case EventKind::Var:
            g_varsPending.push_back(std::move(event.var));
            break;
        case EventKind::Boot:
            g_octaveVersion = event.text;
            appendLine(LineKind::Info, "ectave: 已连接 GNU Octave " + g_octaveVersion);
            requestWorkspace(engine);
            break;
        case EventKind::End:
            if (event.purpose == SeqPurpose::Workspace) {
                g_vars.swap(g_varsPending);
                g_varsPending.clear();
            } else {
                if (g_outstandingCommands > 0) {
                    --g_outstandingCommands;
                }
                if (event.purpose == SeqPurpose::Command && engine.running()) {
                    requestWorkspace(engine);
                }
            }
            break;
        case EventKind::StartFailed:
            appendLine(LineKind::Error,
                       event.failReason == "not-found"
                           ? "ectave: 没找到 Octave — 既没有内置引擎（engines/octave），PATH 上"
                             "也没有 octave-cli/octave。先装一个（Windows/macOS 用官网安装包，"
                             "Linux 用包管理器），再点「重启」；Linux 上也可用 "
                             "scripts/build_engines.sh 打包出自包含引擎"
                       : event.failReason == "conpty-unavailable"
                           ? "ectave: 本机不支持 ConPTY（需要 Windows 10 1809 及以上），"
                             "无法启动交互会话"
                           : "ectave: Octave 启动失败（" + event.failReason + "）");
            break;
        case EventKind::Exited:
            g_outstandingCommands = 0;
            g_vars.clear();
            g_varsPending.clear();
            g_octaveVersion.clear();
            appendLine(LineKind::Dim, "— Octave 进程已退出 —");
            break;
        }
    }
}

} // namespace ectave
