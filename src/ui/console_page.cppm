// ui/console_page.cppm — 控制台页：虚拟列表输出（等宽字体、真实度量软换行、
// 语义着色、贴底跟随）+ 命令栏（ectave.ui.command_input 派生输入）。
module;

#include "eui_ui.h"

export module ectave.ui.console_page;

import std;
import ectave.utils;
import ectave.octave_engine;
import ectave.store;
import ectave.ui.utils;
import ectave.ui.theme;
import ectave.ui.command_input;

// 模块内部实现：写在 ectave::ui 里但**不 export** —— 这些实体因此是模块链接性，
// 本模块之外看不见，达到了「内部实现」的原意。
// 别改回匿名命名空间：内部链接的类型（DisplayRow）会经导出函数的 lambda 与
// std::vector<DisplayRow> 的实例化漏出去，GCC 按 [basic.link]/14 报
// "暴露了 TU 局部实体"（clang 不报，但标准上确实是病式）。
namespace ectave::ui {

struct DisplayRow {
    LineKind kind{LineKind::Output};
    std::string text;
};

// 显示行是逻辑行的派生物（软换行结果），缓存在 UI 层：仅当内容脏或可用宽度
// 变化 > 0.5px 时整体重算（6k 行也只在窗口拉宽时重排一次）。
std::vector<DisplayRow> s_rows;
float s_wrapWidth = -1.0f;

void rebuildRows(float availWidth) {
    if (!g_consoleDirty && std::abs(availWidth - s_wrapWidth) <= 0.5f) {
        return;
    }
    s_rows.clear();
    for (const auto& line : g_console) {
        const std::vector<std::string> subLines = splitLines(line.text);
        bool first = true;
        for (const auto& raw : subLines) {
            std::string prefix;
            if (line.kind == LineKind::Prompt) {
                prefix = first ? "» " : "  ";
            }
            for (auto& wrapped : wrapMonoLine(prefix + raw, availWidth)) {
                s_rows.push_back({line.kind, std::move(wrapped)});
            }
            first = false;
        }
    }
    constexpr std::size_t kMaxRows = 8000;
    if (s_rows.size() > kMaxRows) {
        s_rows.erase(s_rows.begin(),
                     s_rows.begin() + static_cast<std::ptrdiff_t>(s_rows.size() - kMaxRows + 200));
    }
    g_consoleDirty = false;
    s_wrapWidth = availWidth;
}

} // namespace ectave::ui

export namespace ectave::ui {

// 命令栏热区 id。
const inline char kCommandInputId[] = "ectave.cmd.input";

inline void renderConsolePage(core::dsl::Ui& ui, float width, float height,
                              const components::theme::ThemeColorTokens& tokens) {

    const float gap = kShellGap;
    const float listHeight = std::max(40.0f, height - kCommandHeight - gap);
    const float availTextWidth = std::max(40.0f, width - 2.0f * kConsolePadX - kScrollbarWidth - kScrollbarGap);
    rebuildRows(availTextWidth);

    const float lineHeight = kConsoleLineHeight;
    const float totalHeight = static_cast<float>(s_rows.size()) * lineHeight;
    const float maxOffset = std::max(0.0f, totalHeight - listHeight);
    if (g_followOutput) {
        g_consoleOffset = maxOffset;
    } else {
        g_consoleOffset = std::clamp(g_consoleOffset, 0.0f, maxOffset);
    }

    ui.column("ectave.console")
        .size(width, height)
        .gap(gap)
        .content([&] {
            // ---- 输出区：底色面板 + 虚拟列表 ----
            ui.stack("ectave.console.pane")
                .size(width, listHeight)
                .content([&] {
                    ui.rect("ectave.console.bg")
                        .size(width, listHeight)
                        .color(consoleBackground(tokens))
                        .radius(kPanelRadius)
                        .border(1.0f, components::theme::withOpacity(tokens.border, 0.6f))
                        .build();

                    if (s_rows.empty()) {
                        ui.text("ectave.console.empty")
                            .position(kConsolePadX, kConsolePadX)
                            .size(std::max(0.0f, width - 2.0f * kConsolePadX), lineHeight)
                            .text("（无输出）")
                            .fontSize(kConsoleFontSize)
                            .lineHeight(lineHeight)
                            .color(dimText(tokens))
                            .build();
                    } else {
                        components::virtualList(ui, "ectave.console.list")
                            .size(width, listHeight)
                            .itemCount(static_cast<std::int64_t>(s_rows.size()))
                            .rowHeight(lineHeight)
                            .offset(g_consoleOffset)
                            .scrollbarWidth(kScrollbarWidth)
                            .scrollbarGap(kScrollbarGap)
                            .theme(tokens)
                            .transition(fastTransition())
                            .onChange([maxOffset, lineHeight](float value) {
                                g_consoleOffset = value;
                                g_followOutput = value >= maxOffset - lineHeight * 0.75f;
                            })
                            .row([&tokens](core::dsl::Ui& rowUi, const std::string& rowId,
                                            std::int64_t index, float rowWidth, float rowHeight) {
                                const auto& row = s_rows[static_cast<std::size_t>(index)];
                                rowUi.text(rowId + ".text")
                                    .position(kConsolePadX, 0.0f)
                                    .size(std::max(0.0f, rowWidth - 2.0f * kConsolePadX), rowHeight)
                                    .text(row.text)
                                    .fontSize(kConsoleFontSize)
                                    .fontFamily(kMonoFont)
                                    .lineHeight(rowHeight)
                                    .color(rowColor(row.kind, tokens))
                                    .wrap(false)
                                    .verticalAlign(core::VerticalAlign::Top)
                                    .build();
                            })
                            .build();
                    }
                })
                .build();

            // ---- 命令栏 ----
            const float runWidth = 64.0f;
            const float inputWidth = std::max(80.0f, width - runWidth - kButtonGap);
            ui.row("ectave.console.bar")
                .size(width, kCommandHeight)
                .gap(kButtonGap)
                .content([&] {
                    commandInput(ui, kCommandInputId)
                        .size(inputWidth, kCommandHeight)
                        .value(g_commandText)
                        .placeholder("输入 Octave 命令 — Enter 执行 · Shift+Enter 换行")
                        .fontSize(kCommandFontSize)
                        .fontFamily(kMonoFont)
                        .inset(9.0f)
                        .theme(tokens)
                        .transition(fastTransition())
                        .onChange([](const std::string& value) { g_commandText = value; })
                        .onSubmit([] {
                            if (sendCommand(g_octave)) {
                                g_followOutput = true;
                            }
                        })
                        .onEscape([] {
                            if (!g_commandText.empty()) {
                                g_commandText.clear();
                            }
                        })
                        .onHistory([](int direction) { applyHistory(direction); })
                        .build();

                    components::button(ui, "ectave.console.run")
                        .size(runWidth, kCommandHeight)
                        .text("运行")
                        .fontSize(kButtonFontSize)
                        .theme(tokens, true)
                        .textColor(onPrimaryColor())
                        .transition(fastTransition())
                        .onClick([] {
                            if (sendCommand(g_octave)) {
                                g_followOutput = true;
                            }
                        })
                        .build();
                })
                .build();
        })
        .build();
}

} // namespace ectave::ui
