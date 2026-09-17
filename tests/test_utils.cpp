// test_utils.cpp — ectave.utils 的单元测试（纯逻辑，无 UI、无引擎）。
import std;
import ectave.utils;

namespace {

int g_failures = 0;

void check(bool ok, std::string_view what) {
    if (!ok) {
        ++g_failures;
        std::println(std::cerr, "FAIL: {}", what);
    }
}

void testTrim() {
    check(ectave::trim("  abc  ") == "abc", "trim 去首尾空白");
    check(ectave::trim("\t\r\nabc\n") == "abc", "trim 认 \\t\\r\\n");
    check(ectave::trim("   ") == "", "trim 全空白 → 空串");
    check(ectave::trim("") == "", "trim 空串");
    check(ectave::trim("a b") == "a b", "trim 不动中间空白");
}

void testStartsWith() {
    check(ectave::startsWith("error: 未定义", "error:"), "startsWith 命中");
    check(!ectave::startsWith("warning: x", "error:"), "startsWith 不命中");
    check(ectave::startsWith("abc", ""), "startsWith 空前缀恒真");
    check(!ectave::startsWith("ab", "abc"), "startsWith 前缀更长 → 假");
}

void testSplitLines() {
    const auto one = ectave::splitLines("abc");
    check(one.size() == 1 && one[0] == "abc", "splitLines 无换行 → 单元素");

    const auto two = ectave::splitLines("a\nb");
    check(two.size() == 2 && two[0] == "a" && two[1] == "b", "splitLines 两行");

    // 尾随换行会产出一个空尾元素 —— 这是刻意的：Octave 每个输出行都带 \n，
    // 丢掉尾元素会把最后一行吃掉。
    const auto trailing = ectave::splitLines("a\n");
    check(trailing.size() == 2 && trailing[1].empty(), "splitLines 尾随换行保留空尾元素");

    const auto crlf = ectave::splitLines("a\r\nb\r\n");
    check(crlf.size() == 3 && crlf[0] == "a" && crlf[1] == "b" && crlf[2].empty(),
          "splitLines 剥掉 \\r");

    const auto empty = ectave::splitLines("");
    check(empty.size() == 1 && empty[0].empty(), "splitLines 空串 → {\\\"\\\"}");
}

void testUtf8() {
    // "é" = C3 A9（2 字节）；"中" = E4 B8 AD（3 字节）；"😀" = F0 9F 98 80（4 字节）。
    const std::string s = "aé中😀b";
    // 字节下标：a=0, é=1..2, 中=3..5, 😀=6..9, b=10
    check(ectave::utf8Next(s, 0) == 1, "utf8Next ASCII 前进 1");
    check(ectave::utf8Next(s, 1) == 3, "utf8Next 2 字节码点");
    check(ectave::utf8Next(s, 3) == 6, "utf8Next 3 字节码点");
    check(ectave::utf8Next(s, 6) == 10, "utf8Next 4 字节码点");
    check(ectave::utf8Next(s, 10) == 11, "utf8Next 末尾 ASCII");
    check(ectave::utf8Next(s, s.size()) == s.size(), "utf8Next 越界 → size");
    check(ectave::utf8Next(s, 999) == s.size(), "utf8Next 远超界 → size");

    check(ectave::utf8Prev(s, 0) == 0, "utf8Prev 起点");
    check(ectave::utf8Prev(s, 3) == 1, "utf8Prev 跨 2 字节码点");
    check(ectave::utf8Prev(s, 6) == 3, "utf8Prev 跨 3 字节码点");
    check(ectave::utf8Prev(s, 10) == 6, "utf8Prev 跨 4 字节码点");
    check(ectave::utf8Prev(s, 11) == 10, "utf8Prev 末尾 ASCII");
    check(ectave::utf8Prev(s, s.size()) == 10, "utf8Prev size → 最后一码点");

    // 前后互为逆运算（在码点边界上）。串尾是 no-op（next == p == size），此时
    // utf8Prev 会正常退到最后一个码点，所以恒等式只在 next > p 时断言。
    std::size_t codepoints = 0;
    for (std::size_t p = 0; p <= s.size();) {
        const std::size_t next = ectave::utf8Next(s, p);
        if (next == p) {
            check(p == s.size(), "utf8Next 只在串尾 no-op");
            break;
        }
        check(ectave::utf8Prev(s, next) == p, "utf8Prev∘utf8Next 恒等");
        ++codepoints;
        p = next;
    }
    check(codepoints == 5, "整串共 5 个码点（a é 中 😀 b）");
}

} // namespace

int main() {
    testTrim();
    testStartsWith();
    testSplitLines();
    testUtf8();

    if (g_failures == 0) {
        std::println("test_utils: 全部通过");
        return 0;
    }
    std::println(std::cerr, "test_utils: {} 项失败", g_failures);
    return 1;
}
