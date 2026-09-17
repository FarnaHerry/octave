// test_store.cpp — ectave.store 的单元测试。
//
// 只测不碰引擎进程的部分：输出行分类、控制台行模型与截断、命令历史翻页、清屏。
// store 里的 sendCommand/runScript/requestWorkspace/syncFromEngine 都以 OctaveEngine&
// 为参数，需要真起一个 Octave，不在单元测试范围内（那属于端到端冒烟）。
import std;
import ectave.store;
import ectave.utils;
// isPromptLine 定义在这里；store.cppm 对 octave_engine 是普通 import，不透传名字。
import ectave.octave_engine;

namespace {

int g_failures = 0;

void check(bool ok, std::string_view what) {
    if (!ok) {
        ++g_failures;
        std::println(std::cerr, "FAIL: {}", what);
    }
}

// 每个用例前把 store 的全局态复位（store 是全局单例，用例之间会互相污染）。
void resetStore() {
    ectave::clearConsole();
    ectave::g_history.clear();
    ectave::g_historyIndex = static_cast<std::size_t>(-1);
    ectave::g_draft.clear();
    ectave::g_commandText.clear();
    ectave::g_vars.clear();
    ectave::g_varsPending.clear();
    ectave::g_octaveVersion.clear();
    ectave::g_outstandingCommands = 0;
    ectave::g_followOutput = true;
    ectave::g_pane = ectave::Pane::Console;
}

void testClassifyOutputLine() {
    using ectave::LineKind;
    check(ectave::classifyOutputLine("error: 'x' undefined") == LineKind::Error,
          "error: → Error");
    check(ectave::classifyOutputLine("parse error near line 3") == LineKind::Error,
          "parse error → Error");
    check(ectave::classifyOutputLine("warning: implicit conversion") == LineKind::Warning,
          "warning: → Warning");
    check(ectave::classifyOutputLine("ans = 3") == LineKind::Output, "普通输出 → Output");
    check(ectave::classifyOutputLine("") == LineKind::Output, "空行 → Output");

    // 前导空白要先跳掉再判定（Octave 的续行/缩进输出带前导空格）。
    check(ectave::classifyOutputLine("    error: boom") == LineKind::Error,
          "前导空格后仍认 error:");
    check(ectave::classifyOutputLine("\twarning: hmm") == LineKind::Warning,
          "前导 tab 后仍认 warning:");

    // 只认行首前缀：正文里出现 "error:" 不算。
    check(ectave::classifyOutputLine("my error: text") == LineKind::Output,
          "错误前缀必须贴行首");
}

void testAppendLineAndTruncation() {
    resetStore();
    ectave::appendLine(ectave::LineKind::Prompt, "1+1");
    ectave::appendLine(ectave::LineKind::Output, "ans = 2");
    check(ectave::g_console.size() == 2, "appendLine 追加两行");
    check(ectave::g_console[0].kind == ectave::LineKind::Prompt, "第一行 kind 保留");
    check(ectave::g_console[0].text == "1+1", "第一行文本保留");
    check(ectave::g_console[1].text == "ans = 2", "第二行文本保留");
    check(ectave::g_consoleDirty, "appendLine 置脏");
    check(!ectave::g_consoleTruncated, "未触发截断前标志为假");

    // 越过上限：成批丢弃（一次丢到上限以下留 64 行余量），且只置一次标志。
    resetStore();
    for (std::size_t i = 0; i < ectave::kMaxConsoleLines + 100; ++i) {
        ectave::appendLine(ectave::LineKind::Output, "line " + std::to_string(i));
    }
    check(ectave::g_console.size() <= ectave::kMaxConsoleLines, "截断后不超过上限");
    check(ectave::g_consoleTruncated, "截断置标志");
    // 保留的必须是**最新**的行（旧行被丢掉）。
    check(ectave::g_console.back().text ==
              "line " + std::to_string(ectave::kMaxConsoleLines + 99),
          "截断保留最新行");

    resetStore();
    ectave::clearConsole();
    check(ectave::g_console.empty(), "clearConsole 清空");
    check(!ectave::g_consoleTruncated, "clearConsole 复位截断标志");
    check(ectave::g_followOutput, "clearConsole 复位贴底跟随");
}

void testHistory() {
    resetStore();
    // 历史为空时翻页是安全的空操作。
    ectave::applyHistory(-1);
    check(ectave::g_commandText.empty(), "空历史向上翻 → 无变化");
    ectave::applyHistory(+1);
    check(ectave::g_commandText.empty(), "空历史向下翻 → 无变化");

    ectave::g_history = {"first", "second", "third"};

    // 当前草稿要能回来：翻上去再翻到底应恢复。
    ectave::g_commandText = "draft-here";
    ectave::applyHistory(-1);
    check(ectave::g_commandText == "third", "向上翻到最近一条");
    ectave::applyHistory(-1);
    check(ectave::g_commandText == "second", "再向上");
    ectave::applyHistory(-1);
    check(ectave::g_commandText == "first", "翻到最旧");
    ectave::applyHistory(-1);
    check(ectave::g_commandText == "first", "最旧处继续向上 → 停在原地");

    ectave::applyHistory(+1);
    check(ectave::g_commandText == "second", "向下回到较新一条");
    ectave::applyHistory(+1);
    check(ectave::g_commandText == "third", "继续向下");
    ectave::applyHistory(+1);
    check(ectave::g_commandText == "draft-here", "越过最后一条 → 恢复草稿");
    check(ectave::g_historyIndex == static_cast<std::size_t>(-1), "恢复草稿后复位索引");

    // 没在翻历史时向下翻不应改动输入。
    ectave::g_commandText = "typing";
    ectave::applyHistory(+1);
    check(ectave::g_commandText == "typing", "未翻历史时向下 → 不动输入");
}

// 提示符形状判定：octave 不认 PS1，读线程只能按形状丢回显行。判错的代价是
// 要么控制台继续漏回显，要么把用户真实输出吃掉——两侧都要卡住。
void testIsPromptLine() {
    // 命中：默认 PS1 `\s:\#>` 的展开，行内可能跟着内核回显的命令原文。
    check(ectave::isPromptLine("octave:1> "), "提示符（空闲态）");
    check(ectave::isPromptLine("octave:1> 1+1"), "提示符 + 回显的命令");
    check(ectave::isPromptLine("octave:42> x = 3"), "多位数命令号");
    check(ectave::isPromptLine("octave:7>"), "无尾随空格也算");
    check(ectave::isPromptLine("octave-cli:3> "), "程序名带连字符");

    // 不命中：真实输出不能被吃掉。哨兵行与协议行也必须安全通过这一关。
    check(!ectave::isPromptLine("ans = 2"), "普通结果行");
    check(!ectave::isPromptLine("error: 未定义"), "error: 无数字+尖括号");
    check(!ectave::isPromptLine("octave:> 1"), "缺命令号");
    check(!ectave::isPromptLine("octave:1 1+1"), "缺尖括号");
    check(!ectave::isPromptLine("octave:1>x"), "'>' 后不是空格");
    check(!ectave::isPromptLine(":1> "), "缺程序名");
    check(!ectave::isPromptLine(""), "空行");
    check(!ectave::isPromptLine("%a1b2-7%"), "哨兵行（'%' 起头）");
    check(!ectave::isPromptLine("ECTVAR|x|double|[1,1]"), "ECTVAR 协议行");
    check(!ectave::isPromptLine("ECTAVE_BOOT|10.3.0"), "引导上报行");
}

// 哨兵解析：这是命令完成信号的唯一来源，错了会让 End 事件永不触发（UI 卡在
// 「执行中…」）同时把哨兵行漏进控制台。曾经的两个 off-by-one 就出在这里。
void testParseSentinel() {
    const auto one = ectave::parseSentinel("%a1b2c3-1%", "a1b2c3");
    check(one.has_value() && *one == "1", "单个 seq → \"1\"（不是 \"\"）");

    const auto two = ectave::parseSentinel("%a1b2c3-42%", "a1b2c3");
    check(two.has_value() && *two == "42", "两位 seq");

    const auto three = ectave::parseSentinel("%a1b2c3-100%", "a1b2c3");
    check(three.has_value() && *three == "100", "三位 seq");

    check(ectave::parseSentinel("%a1b2c3-1%", "ffffff").has_value() == false, "token 不符");
    check(ectave::parseSentinel("%a1b2c3-1", "a1b2c3").has_value() == false, "缺结尾 %");
    check(ectave::parseSentinel("%a1b2c3-%", "a1b2c3").has_value() == false, "缺 seq");
    check(ectave::parseSentinel("%a1b2c3-1x%", "a1b2c3").has_value() == false, "seq 非纯数字");
    check(ectave::parseSentinel("ans = 2", "a1b2c3").has_value() == false, "普通输出");
    check(ectave::parseSentinel("", "a1b2c3").has_value() == false, "空行");
}

void testViewStateDefaults() {
    resetStore();
    check(ectave::g_pane == ectave::Pane::Console, "默认停在控制台页");
    check(ectave::g_showWorkspace, "默认显示工作区面板");
    check(ectave::g_scriptText.find("ectave 脚本") != std::string::npos,
          "脚本页有初始说明文本");
}

} // namespace

int main() {
    testClassifyOutputLine();
    testAppendLineAndTruncation();
    testHistory();
    testIsPromptLine();
    testParseSentinel();
    testViewStateDefaults();

    if (g_failures == 0) {
        std::println("test_store: 全部通过");
        return 0;
    }
    std::println(std::cerr, "test_store: {} 项失败", g_failures);
    return 1;
}
