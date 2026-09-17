// ui/script_page.cppm — 脚本页：多行编辑区（内置 multiline input）+ 运行工具条。
// 「运行脚本」把整个编辑区一次性发给 Octave（stdin 逐语句执行，天然支持多行）。
module;

#include "eui_ui.h"

export module ectave.ui.script_page;

import std;
import ectave.octave_engine;
import ectave.store;
import ectave.ui.utils;
import ectave.ui.theme;

export namespace ectave::ui {

inline void renderScriptPage(core::dsl::Ui& ui, float width, float height,
                             const components::theme::ThemeColorTokens& tokens) {
    ui.column("ectave.script")
        .size(width, height)
        .gap(kShellGap)
        .content([&] {
            // 工具条。
            ui.row("ectave.script.bar")
                .size(width, kButtonHeight)
                .gap(kButtonGap)
                .content([&] {
                    components::button(ui, "ectave.script.run")
                        .size(88.0f, kButtonHeight)
                        .text("运行脚本")
                        .fontSize(kButtonFontSize)
                        .theme(tokens, true)
                        .textColor(onPrimaryColor())
                        .transition(fastTransition())
                        .onClick([] {
                            if (runScript(g_octave)) {
                                g_followOutput = true;
                            }
                        })
                        .build();
                    components::button(ui, "ectave.script.clear")
                        .size(56.0f, kButtonHeight)
                        .text("清空")
                        .fontSize(kButtonFontSize)
                        .theme(tokens, false)
                        .transition(fastTransition())
                        .onClick([] { g_scriptText.clear(); })
                        .build();
                    ui.text("ectave.script.hint")
                        .size(std::max(0.0f, width - 88.0f - 56.0f - 2.0f * kButtonGap), kButtonHeight)
                        .text("整个编辑区一次执行 · F5")
                        .fontSize(kCompactButtonFontSize)
                        .lineHeight(kButtonHeight)
                        .color(dimText(tokens))
                        .build();
                })
                .build();

            // 编辑区：内置 multiline input（Enter 插换行，内部带滚动与撤销）。
            const float editorHeight = std::max(60.0f, height - kButtonHeight - kShellGap);
            components::input(ui, "ectave.script.editor")
                .size(width, editorHeight)
                .value(g_scriptText)
                .placeholder("% 在这里写 Octave 脚本…")
                .multiline(true)
                .fontSize(kCommandFontSize)
                .fontFamily(kMonoFont)
                .inset(10.0f)
                .theme(tokens)
                .transition(fastTransition())
                .onChange([](const std::string& value) { g_scriptText = value; })
                .build();
        })
        .build();
}

} // namespace ectave::ui
