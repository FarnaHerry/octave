// ui/utils.cppm — 布局常量（设计逻辑像素，DslAppConfig::uiScale(kUI) 负责原生
// 缩放，任何值都不再自乘）、等宽字体度量软换行、文本截断。
module;

#include "eui_ui.h"   // core::TextPrimitive::measureTextWidth（真实字体度量）

export module ectave.ui.utils;

import std;
import ectave.utils;

export namespace ectave::ui {

// ---- 缩放与窗口 ----
constexpr float kUI = 1.3f;
constexpr float kWindowDesignWidth = 1160.0f;
constexpr float kWindowDesignHeight = 780.0f;
constexpr float kShellPad = 8.0f;
constexpr float kShellGap = 6.0f;

// ---- 按钮设计令牌 ----
constexpr float kButtonHeight = 26.0f;
constexpr float kButtonFontSize = 12.0f;
constexpr float kCompactButtonHeight = 24.0f;
constexpr float kCompactButtonFontSize = 11.0f;
constexpr float kButtonGap = 8.0f;
constexpr float kButtonRadius = 8.0f;

// ---- 结构 ----
constexpr float kToolbarHeight = 40.0f;
constexpr float kStatusHeight = 24.0f;
constexpr float kCommandHeight = 40.0f;
constexpr float kWorkspaceWidth = 250.0f;
constexpr float kWorkspaceRowHeight = 30.0f;
constexpr float kPanelPad = 10.0f;
constexpr float kPanelRadius = 10.0f;

// ---- 控制台 ----
const inline char kMonoFont[] = "JetBrainsMono.ttf";
constexpr float kConsoleFontSize = 12.5f;
constexpr float kConsoleLineHeight = 18.0f;
constexpr float kConsolePadX = 8.0f;
constexpr float kCommandFontSize = 13.0f;

// ---- 滚动条（同 tinynext/apitab 规范：显式令牌，不用 EUI 默认值）----
constexpr float kScrollbarWidth = 4.0f;
constexpr float kScrollbarGap = 6.0f;

inline float measureMono(const std::string& text, float fontSize = kConsoleFontSize) {
    return core::TextPrimitive::measureTextWidth(text, kMonoFont, fontSize);
}

// 按等宽字体真实度量截断，超宽补省略号。
inline std::string truncateMono(const std::string& text, float maxWidth, float fontSize) {
    if (measureMono(text, fontSize) <= maxWidth || text.empty()) {
        return text;
    }
    const float ellipsisW = measureMono("…", fontSize);
    std::size_t end = text.size();
    for (std::size_t pos = text.size(); pos > 0;) {
        pos = utf8Prev(text, pos);
        if (measureMono(text.substr(0, pos), fontSize) + ellipsisW <= maxWidth) {
            end = pos;
            break;
        }
        if (pos == 0) {
            end = 0;
            break;
        }
    }
    return text.substr(0, end) + "…";
}

// 等宽字体下的贪心软换行：保留 token（词+尾随空格）以维持矩阵列对齐；
// 单 token 超宽按码点硬切。measure 注入便于将来换字体/字号。
inline std::vector<std::string> wrapMonoLine(const std::string& text, float maxWidth) {
    std::vector<std::string> rows;
    if (maxWidth <= 1.0f) {
        rows.push_back(text);
        return rows;
    }
    if (text.empty()) {
        rows.push_back("");
        return rows;
    }
    std::string current;
    const auto flush = [&] {
        rows.push_back(current);
        current.clear();
    };
    std::size_t pos = 0;
    while (pos < text.size()) {
        // token = 直到下一个空格（含）
        std::size_t next = pos;
        while (next < text.size() && text[next] != ' ') {
            ++next;
        }
        if (next < text.size()) {
            ++next;  // 吸收尾随空格
        }
        std::string token = text.substr(pos, next - pos);
        pos = next;
        if (!current.empty() && measureMono(current + token) <= maxWidth) {
            current += token;
            continue;
        }
        if (current.empty() || measureMono(token) <= maxWidth) {
            // token 能放进新行（含 current 非空但 token 超宽时：token 放不下，走硬切）
            if (current.empty() || measureMono(current + token) <= maxWidth) {
                current += token;
            } else {
                flush();
                current = token;
            }
            continue;
        }
        // token 本身超宽：按码点硬切填满当前行
        flush();
        std::size_t tpos = 0;
        std::string piece;
        while (tpos < token.size()) {
            const std::size_t cp = utf8Next(token, tpos);  // 当前码点的结束位置
            const std::string candidate = piece + token.substr(tpos, cp - tpos);
            if (piece.empty() || measureMono(candidate) <= maxWidth) {
                piece += token.substr(tpos, cp - tpos);
                tpos = cp;
            } else {
                rows.push_back(piece);
                piece.clear();
            }
        }
        current = std::move(piece);
    }
    // 去掉尾部纯空白显示行的抖动：保留最后一行（可为空串）
    while (!current.empty() && current.back() == ' ') {
        current.pop_back();
    }
    flush();
    return rows;
}

} // namespace ectave::ui
