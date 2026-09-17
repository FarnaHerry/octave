// ui/workspace_page.cppm — 工作区面板：who+class+size 快照列表。
// 点变量行 → 把 disp('name') 填进命令栏并聚焦（不自动执行，给用户改的余地）。
module;

#include "eui_ui.h"

export module ectave.ui.workspace_page;

import std;
import ectave.octave_engine;
import ectave.store;
import ectave.ui.utils;
import ectave.ui.theme;
import ectave.ui.console_page;

export namespace ectave::ui {

inline void renderWorkspacePanel(core::dsl::Ui& ui, float width, float height,
                                 const components::theme::ThemeColorTokens& tokens) {
    const float headerHeight = 28.0f;
    const float listHeight = std::max(40.0f, height - headerHeight - kShellGap);
    const std::size_t count = g_vars.size();

    ui.column("ectave.workspace")
        .size(width, height)
        .gap(kShellGap)
        .content([&] {
            // 标题行：名称 + 计数 + 刷新。
            ui.row("ectave.workspace.header")
                .size(width, headerHeight)
                .gap(kButtonGap)
                .alignItems(core::Align::CENTER)
                .content([&] {
                    ui.text("ectave.workspace.title")
                        .size(48.0f, headerHeight)
                        .text("工作区")
                        .fontSize(13.0f)
                        .lineHeight(headerHeight)
                        .fontWeight(640)
                        .color(tokens.text)
                        .build();
                    ui.text("ectave.workspace.count")
                        .size(width - 48.0f - 56.0f - 2.0f * kButtonGap, headerHeight)
                        .text(count == 0 ? "空" : std::to_string(count) + " 个变量")
                        .fontSize(kCompactButtonFontSize)
                        .lineHeight(headerHeight)
                        .color(dimText(tokens))
                        .build();
                    components::button(ui, "ectave.workspace.refresh")
                        .size(56.0f, kCompactButtonHeight)
                        .text("刷新")
                        .fontSize(kCompactButtonFontSize)
                        .theme(tokens, false)
                        .disabled(!g_octave.running())
                        .transition(fastTransition())
                        .onClick([] { requestWorkspace(g_octave); })
                        .build();
                })
                .build();

            ui.stack("ectave.workspace.pane")
                .size(width, listHeight)
                .content([&] {
                    ui.rect("ectave.workspace.bg")
                        .size(width, listHeight)
                        .color(panelBackground(tokens))
                        .radius(kPanelRadius)
                        .border(1.0f, components::theme::withOpacity(tokens.border, 0.6f))
                        .build();

                    if (count == 0) {
                        ui.text("ectave.workspace.empty")
                            .position(kPanelPad, kPanelPad)
                            .size(std::max(0.0f, width - 2.0f * kPanelPad), 40.0f)
                            .text("暂无变量 — 在工作区执行赋值后自动出现")
                            .fontSize(kCompactButtonFontSize)
                            .lineHeight(18.0f)
                            .color(dimText(tokens))
                            .wrap(true)
                            .build();
                    } else {
                        components::virtualList(ui, "ectave.workspace.list")
                            .size(width - 2.0f, listHeight - 2.0f)
                            .position(1.0f, 1.0f)
                            .itemCount(static_cast<std::int64_t>(count))
                            .rowHeight(kWorkspaceRowHeight)
                            .scrollbarWidth(kScrollbarWidth)
                            .scrollbarGap(kScrollbarGap)
                            .theme(tokens)
                            .row([](core::dsl::Ui& rowUi, const std::string& rowId,
                                    std::int64_t index, float rowWidth, float rowHeight) {
                                const std::size_t varIndex = static_cast<std::size_t>(index);
                                rowUi.stack(rowId)
                                    .size(rowWidth, rowHeight)
                                    .content([&] {
                                        rowUi.rect(rowId + ".hit")
                                            .size(rowWidth, rowHeight)
                                            .color(eui::Color{0, 0, 0, 0})
                                            .states(eui::Color{0, 0, 0, 0},
                                                    components::theme::withOpacity(
                                                        static_cast<const components::theme::ThemeColorTokens&>(
                                                            appTokens())
                                                            .surfaceHover, 0.6f),
                                                    components::theme::withOpacity(appTokens().surfaceActive, 0.7f))
                                            .onClick([varIndex] {
                                                // 0.5.9 无公开 setFocusedId，无法抢焦点回命令栏，
                                                // 因此单击直接执行 disp（检查变量的最快路径）。
                                                if (varIndex < g_vars.size()) {
                                                    g_commandText = "disp('" + g_vars[varIndex].name + "')";
                                                    sendCommand(g_octave);
                                                    g_followOutput = true;
                                                }
                                            })
                                            .build();

                                        // 名称左（等宽，截断），类型/尺寸右（暗色）。
                                        const std::string meta =
                                            g_vars[varIndex].dims + " · " + g_vars[varIndex].cls;
                                        const float metaWidth = std::min(96.0f, rowWidth * 0.42f);
                                        const std::string name = truncateMono(
                                            g_vars[varIndex].name, rowWidth - metaWidth - 24.0f, 12.0f);
                                        rowUi.text(rowId + ".name")
                                            .position(8.0f, 0.0f)
                                            .size(std::max(0.0f, rowWidth - metaWidth - 24.0f), rowHeight)
                                            .text(name)
                                            .fontSize(12.0f)
                                            .fontFamily(kMonoFont)
                                            .lineHeight(rowHeight)
                                            .color(appTokens().text)
                                            .verticalAlign(core::VerticalAlign::Center)
                                            .build();
                                        rowUi.text(rowId + ".meta")
                                            .size(metaWidth, rowHeight)
                                            .position(rowWidth - metaWidth - 8.0f, 0.0f)
                                            .text(truncateMono(meta, metaWidth, 11.0f))
                                            .fontSize(11.0f)
                                            .lineHeight(rowHeight)
                                            .color(dimText(appTokens()))
                                            .horizontalAlign(core::HorizontalAlign::Right)
                                            .verticalAlign(core::VerticalAlign::Center)
                                            .build();
                                    })
                                    .build();
                            })
                            .build();
                    }
                })
                .build();
        })
        .build();
}

} // namespace ectave::ui
