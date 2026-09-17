// test_engine.cpp — 引擎端到端：真起一个 octave 子进程，走完整 PTY 读写链路。
//
// 为什么要有这个测试：这条链路（PTY 回显、提示符、哨兵对齐、CRLF 剥行）用假数据
// 覆盖不了，而它一次藏了四个只有真跑才暴露的缺陷——引导回显漏进控制台、按 PS1 判定
// 回显失效（octave 根本不认 PS1）、行尾 '\r' 让哨兵永远匹配不上、哨兵解析的两个
// off-by-one。全都是「编译通过、单元测试全绿、界面照样出错」的那一类。
//
// 无头可跑：不建窗口。读线程调的 core::platform::requestUiUpdate() 最终落到
// glfwPostEmptyEvent()，GLFW 未初始化时 _GLFW_REQUIRE_INIT() 直接 return。
// 机器上没有 octave 就跳过（退出码 77 = CTest SKIP），不算失败。
import std;
import ectave.octave_engine;

using namespace std::chrono_literals;

namespace {

int g_failures = 0;

void check(bool ok, std::string_view what) {
    if (!ok) {
        ++g_failures;
        std::println(std::cerr, "FAIL: {}", what);
    }
}

// 含 ESC 或提示符形状（`<名字>:<数字>> `）的行都不该出现在控制台输出里：
// 前者是 readline 转义没压住，后者是回显/提示符没丢干净。
bool looksLikePrompt(std::string_view s) {
    const auto pos = s.find(':');
    if (pos == std::string_view::npos || pos == 0) {
        return false;
    }
    std::size_t i = pos + 1;
    const std::size_t digits = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        ++i;
    }
    return i > digits && i < s.size() && s[i] == '>';
}

// 哨兵行 `%<token>-<seq>%`：匹配上就该被消费掉，漏到这里说明解析又坏了。
bool looksLikeSentinel(std::string_view s) {
    return s.size() > 2 && s.front() == '%' && s.back() == '%' && s.find('-') != std::string_view::npos;
}

struct Trace {
    std::string version;
    std::vector<std::string> output;
    bool startFailed = false;
    std::string failReason;
};

// 排空事件直到 stopWhen 返回 true 或超时。返回是否因 stopWhen 而停。
template <typename StopWhen>
bool pump(ectave::OctaveEngine& engine, std::chrono::steady_clock::duration budget,
          Trace& trace, StopWhen stopWhen) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& ev : engine.drainEvents()) {
            switch (ev.kind) {
            case ectave::EventKind::Output:
                trace.output.push_back(std::move(ev.text));
                break;
            case ectave::EventKind::Boot:
                trace.version = std::move(ev.text);
                break;
            case ectave::EventKind::StartFailed:
                trace.startFailed = true;
                trace.failReason = std::move(ev.failReason);
                break;
            default:
                break;
            }
            if (stopWhen(ev)) {
                return true;
            }
        }
        if (trace.startFailed) {
            return false;
        }
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

bool isEnd(const ectave::EngineEvent& ev, ectave::SeqPurpose purpose) {
    return ev.kind == ectave::EventKind::End && ev.purpose == purpose;
}

} // namespace

int main() {
    auto& engine = ectave::g_octave;
    engine.start();

    // ---- 1. 引导握手：等 Boot 事件（带版本号）----
    Trace trace;
    const bool booted = pump(engine, 30s, trace, [](const ectave::EngineEvent& ev) {
        return ev.kind == ectave::EventKind::Boot;
    });

    if (trace.startFailed) {
        // 只有「找不到引擎」才跳过；其它启动失败照常报错。
        engine.stop();
        if (trace.failReason == "not-found") {
            std::println("test_engine: 跳过（本机没有 octave-cli/octave）");
            return 77;
        }
        std::println(std::cerr, "test_engine: 引擎启动失败: {}", trace.failReason);
        return 1;
    }

    check(booted, "引导握手在 30s 内完成（ECTAVE_BOOT）");
    check(!trace.version.empty(), "Boot 事件带回版本号");
    check(trace.version.find('\r') == std::string::npos, "版本号不含 '\\r'（CRLF 剥行）");

    // 引导阶段的输出只该有 ectave 自己那几条，不该夹带回显/转义/哨兵。
    for (const auto& line : trace.output) {
        check(line.find('\x1b') == std::string::npos, "引导输出无 ESC 转义");
        check(!looksLikePrompt(line), "引导输出无提示符回显");
        check(!looksLikeSentinel(line), "引导阶段哨兵行已被消费");
    }

    // ---- 2. 真跑一条命令，确认 End 哨兵与结果都回来了 ----
    trace.output.clear();
    check(engine.send("1+1", ectave::SeqPurpose::Command), "send() 接受命令");

    const bool done = pump(engine, 30s, trace, [](const ectave::EngineEvent& ev) {
        return isEnd(ev, ectave::SeqPurpose::Command);
    });
    check(done, "命令哨兵匹配上（End 事件触发）—— 这是 UI 结束「执行中…」的唯一信号");

    bool sawResult = false;
    for (const auto& line : trace.output) {
        if (line.find("ans = 2") != std::string::npos) {
            sawResult = true;
        }
        check(line.find('\x1b') == std::string::npos, "命令输出无 ESC 转义");
        check(!looksLikePrompt(line), "命令输出无提示符回显");
        check(!looksLikeSentinel(line), "命令阶段哨兵行已被消费");
    }
    check(sawResult, "拿到 1+1 的结果 ans = 2");

    engine.stop();

    if (g_failures == 0) {
        std::println("test_engine: 全部通过");
        return 0;
    }
    std::println(std::cerr, "test_engine: {} 项失败", g_failures);
    return 1;
}
