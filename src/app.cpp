// app.cpp — ectave：EUI-NEO 套壳 GNU Octave CLI 的工作台应用。
//
// main() 来自 compat.eui-neo 的 app-main feature（core/app/glfw_app_main.cpp）；
// 本 TU 提供 app::dslAppConfig() + app::compose()，负责：配置、常驻壳
// （工具栏 / 页面分发 / 状态栏）、全局按键、引擎事件摄取（store.syncFromEngine）。
// 页面在 ectave.ui.* 模块里各自渲染，store 拥有会话状态，engine 拥有子进程。
//
//   ectave.utils            纯字符串工具
//   ectave.octave_engine    octave 子进程 + 哨兵分块协议 + 事件队列
//   ectave.store            控制台/历史/工作区/视图状态（仅 UI 线程读写）
//   ectave.ui.utils         设计逻辑像素令牌 + 等宽度量软换行
//   ectave.ui.theme         配色令牌（Octave 蓝）+ 连接状态文案
//   ectave.ui.command_input 命令栏输入（Enter 提交 / Shift+Enter 换行 / ↑↓ 历史）
//   ectave.ui.console_page  输出虚拟列表 + 命令栏
//   ectave.ui.workspace_page 工作区变量面板
//   ectave.ui.script_page   脚本编辑页
#include <eui_neo.h>

import std;
import ectave.octave_engine;
import ectave.store;
import ectave.ui.utils;
import ectave.ui.theme;
import ectave.ui.console_page;
import ectave.ui.workspace_page;
import ectave.ui.script_page;

namespace app {

using ectave::EngineState;
using ectave::g_octave;
using ectave::Pane;

// ---- 全局快捷键（焦点元素未消费的按键落到这里，见 docs/事件.md）----

void handleGlobalKey(const eui::KeyEvent& event) {
    if (!event.isDown()) {
        return;
    }
    const bool shortcut = event.modifiers.shortcut();
    if (shortcut && event.key == eui::InputKey::L) {
        ectave::clearConsole();
    } else if (event.key == eui::InputKey::F5) {
        if (ectave::g_pane == Pane::Script) {
            if (ectave::runScript(g_octave)) {
                ectave::g_followOutput = true;
            }
        }
    } else if (shortcut && event.key == eui::InputKey::Digit1) {
        ectave::g_pane = Pane::Console;
    } else if (shortcut && event.key == eui::InputKey::Digit2) {
        ectave::g_pane = Pane::Script;
    } else {
        return;  // 未处理：不额外唤醒
    }
    core::platform::requestUiUpdate();
}

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("ectave · GNU Octave 工作台")
        .pageId("ectave")
        .clearColor({0.045f, 0.05f, 0.062f, 1.0f})
        // 原生全局缩放：布局全部按设计逻辑像素书写；窗口物理尺寸 = 设计尺寸 * kUI。
        .uiScale(ectave::ui::kUI)
        .windowSize(static_cast<int>(ectave::ui::kWindowDesignWidth * ectave::ui::kUI),
                    static_cast<int>(ectave::ui::kWindowDesignHeight * ectave::ui::kUI))
        .fps(0.0)  // 自动匹配显示器刷新率
        .textFont("NotoSansSC-Regular.ttf")
        .iconFont("FontAwesome7.otf")
        .onKeyEvent(handleGlobalKey);
    return config;
}

// ---- 常驻壳 ----

void compose(eui::Ui& ui, const eui::Screen& screen) {
    // 启动一次：拉起 octave 子进程（fork+exec 很快，UI 线程直接做；
    // 后续输出经读线程事件信箱异步回 UI）。
    static bool booted = false;
    if (!booted) {
        booted = true;
        ectave::appendLine(ectave::LineKind::Info,
                           "ectave — GNU Octave 命令行工作台（EUI-NEO 壳）");
        g_octave.start();
    }
    // 排空引擎事件信箱 → store（每帧开头，之后所有页面读同一份状态）。
    ectave::syncFromEngine(g_octave);

    const auto& tokens = ectave::ui::appTokens();
    const float width = screen.width;
    const float height = screen.height;
    const float innerWidth = std::max(240.0f, width - 2.0f * ectave::ui::kShellPad);
    const float innerHeight = std::max(120.0f, height - 2.0f * ectave::ui::kShellPad);

    // 窄窗口自动收起工作区（阈值是设计逻辑像素）。
    const bool showWorkspace = ectave::g_showWorkspace && innerWidth >= 760.0f;
    const float mainHeight = std::max(80.0f, innerHeight - ectave::ui::kToolbarHeight -
                                                 ectave::ui::kStatusHeight - 2.0f * ectave::ui::kShellGap);
    const float consoleWidth = std::max(240.0f, innerWidth - (showWorkspace ? ectave::ui::kWorkspaceWidth +
                                                                                       ectave::ui::kShellGap
                                                                             : 0.0f));

    ui.column("ectave.root")
        .size(width, height)
        .padding(ectave::ui::kShellPad)
        .gap(ectave::ui::kShellGap)
        .content([&] {
            // ---- 工具栏 ----
            ui.row("ectave.toolbar")
                .size(innerWidth, ectave::ui::kToolbarHeight)
                .gap(ectave::ui::kButtonGap)
                .alignItems(core::Align::CENTER)
                .content([&] {
                    ui.text("ectave.toolbar.title")
                        .size(52.0f, ectave::ui::kToolbarHeight)
                        .text("ectave")
                        .fontSize(16.0f)
                        .fontWeight(720)
                        .lineHeight(ectave::ui::kToolbarHeight)
                        .color(tokens.primary)
                        .build();

                    components::segmented(ui, "ectave.toolbar.pages")
                        .size(150.0f, ectave::ui::kButtonHeight)
                        .items({"控制台", "脚本"})
                        .selected(ectave::g_pane == Pane::Console ? 0 : 1)
                        .theme(tokens)
                        .transition(ectave::ui::fastTransition())
                        .onChange([](int index) {
                            ectave::g_pane = index == 1 ? Pane::Script : Pane::Console;
                        })
                        .build();

                    // 连接状态胶囊：圆点 + 文案。
                    const auto dot = ectave::ui::connectionState(g_octave);
                    const std::string label = ectave::ui::connectionLabel(g_octave);
                    ui.stack("ectave.toolbar.status")
                        .size(170.0f, ectave::ui::kToolbarHeight)
                        .content([&] {
                            ui.rect("ectave.toolbar.status.dot")
                                .position(0.0f, (ectave::ui::kToolbarHeight - 8.0f) * 0.5f)
                                .size(8.0f, 8.0f)
                                .radius(4.0f)
                                .color(ectave::ui::connectionDotColor(dot, tokens))
                                .transition(ectave::ui::fastTransition())
                                .build();
                            ui.text("ectave.toolbar.status.label")
                                .position(16.0f, 0.0f)
                                .size(154.0f, ectave::ui::kToolbarHeight)
                                .text(label)
                                .fontSize(ectave::ui::kCompactButtonFontSize)
                                .lineHeight(ectave::ui::kToolbarHeight)
                                .color(ectave::ui::dimText(tokens))
                                .build();
                        })
                        .build();

                    ui.rect("ectave.toolbar.spacer")
                        .size(0.0f, ectave::ui::kToolbarHeight)
                        .flexGrow(1.0f)
                        .build();

                    components::button(ui, "ectave.toolbar.interrupt")
                        .size(56.0f, ectave::ui::kCompactButtonHeight)
                        .text("中断")
                        .fontSize(ectave::ui::kCompactButtonFontSize)
                        .theme(tokens, false)
                        .disabled(ectave::g_outstandingCommands == 0 || !g_octave.running())
                        .transition(ectave::ui::fastTransition())
                        .onClick([] {
                            g_octave.interrupt();
                            ectave::appendLine(ectave::LineKind::Info, "ectave: 已发送中断 (SIGINT)");
                        })
                        .build();

                    components::button(ui, "ectave.toolbar.restart")
                        .size(56.0f, ectave::ui::kCompactButtonHeight)
                        .text("重启")
                        .fontSize(ectave::ui::kCompactButtonFontSize)
                        .theme(tokens, false)
                        .transition(ectave::ui::fastTransition())
                        .onClick([] {
                            // stop() 里有界 join，放后台线程防卡 UI。
                            static std::atomic<bool> restarting{false};
                            if (restarting.exchange(true)) {
                                return;
                            }
                            ectave::appendLine(ectave::LineKind::Dim, "— 正在重启 Octave —");
                            std::thread([] {
                                g_octave.restart();
                                restarting.store(false);
                                core::platform::requestUiUpdate();
                            }).detach();
                        })
                        .build();

                    components::button(ui, "ectave.toolbar.clear")
                        .size(56.0f, ectave::ui::kCompactButtonHeight)
                        .text("清屏")
                        .fontSize(ectave::ui::kCompactButtonFontSize)
                        .theme(tokens, false)
                        .transition(ectave::ui::fastTransition())
                        .onClick([] { ectave::clearConsole(); })
                        .build();

                    components::button(ui, "ectave.toolbar.workspace")
                        .size(64.0f, ectave::ui::kCompactButtonHeight)
                        .text("工作区")
                        .fontSize(ectave::ui::kCompactButtonFontSize)
                        .theme(tokens, ectave::g_showWorkspace)
                        .transition(ectave::ui::fastTransition())
                        .onClick([] { ectave::g_showWorkspace = !ectave::g_showWorkspace; })
                        .build();
                })
                .build();

            // ---- 主区：页面 + 工作区 ----
            ui.row("ectave.main")
                .size(innerWidth, mainHeight)
                .gap(ectave::ui::kShellGap)
                .content([&] {
                    if (ectave::g_pane == Pane::Console) {
                        ectave::ui::renderConsolePage(ui, consoleWidth, mainHeight, tokens);
                    } else {
                        ectave::ui::renderScriptPage(ui, innerWidth, mainHeight, tokens);
                    }
                    if (ectave::g_pane == Pane::Console && showWorkspace) {
                        ectave::ui::renderWorkspacePanel(ui, ectave::ui::kWorkspaceWidth, mainHeight, tokens);
                    }
                })
                .build();

            // ---- 状态栏：快捷键提示 + 计数 ----
            ui.row("ectave.statusbar")
                .size(innerWidth, ectave::ui::kStatusHeight)
                .content([&] {
                    ui.text("ectave.statusbar.hints")
                        .size(innerWidth - 170.0f, ectave::ui::kStatusHeight)
                        .text("Enter 执行 · Shift+Enter 换行 · ↑↓ 历史 · Esc 清空草稿 · Ctrl+L 清屏 · F5 运行脚本 · Ctrl+1/2 切页")
                        .fontSize(11.0f)
                        .lineHeight(ectave::ui::kStatusHeight)
                        .color(ectave::ui::dimText(tokens))
                        .build();
                    ui.text("ectave.statusbar.counts")
                        .size(170.0f, ectave::ui::kStatusHeight)
                        .text(std::to_string(ectave::g_console.size()) + " 行 · " +
                              std::to_string(ectave::g_vars.size()) + " 变量")
                        .fontSize(11.0f)
                        .lineHeight(ectave::ui::kStatusHeight)
                        .horizontalAlign(eui::HorizontalAlign::Right)
                        .color(ectave::ui::dimText(tokens))
                        .build();
                })
                .build();
        })
        .build();
}

} // namespace app
