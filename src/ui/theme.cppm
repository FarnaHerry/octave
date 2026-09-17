// ui/theme.cppm — 应用配色令牌（Octave 蓝主色）+ 控制台行色 + 动效时长。
// 基调直接用 components::theme::dark()，只覆写品牌色与控制台语义色。
module;

#include "eui_ui.h"

export module ectave.ui.theme;

import std;
import ectave.octave_engine;
import ectave.store;
import ectave.ui.utils;

export namespace ectave::ui {

inline const components::theme::ThemeColorTokens& appTokens() {
    static const components::theme::ThemeColorTokens tokens = [] {
        auto t = components::theme::dark();
        // Octave 品牌蓝，暗底上保持 AA 以上对比度。
        t.primary = {0.36f, 0.63f, 0.96f, 1.0f};
        return t;
    }();
    return tokens;
}

// 主色按钮/图标文字（apitab 规则：primary 按钮要显式 textColor）。
inline eui::Color onPrimaryColor() {
    return {0.98f, 0.99f, 1.0f, 1.0f};
}

inline eui::Color consoleBackground(const components::theme::ThemeColorTokens& t) {
    return components::theme::withAlpha(t.background, 1.0f);
}

inline eui::Color panelBackground(const components::theme::ThemeColorTokens& t) {
    return t.surface;
}

// 控制台行的前景色（按 LineKind 语义着色）。
inline eui::Color rowColor(LineKind kind, const components::theme::ThemeColorTokens& t) {
    switch (kind) {
    case LineKind::Prompt:
        return {0.52f, 0.76f, 1.0f, 1.0f};   // 用户输入回显：亮蓝
    case LineKind::Error:
        return {0.95f, 0.45f, 0.40f, 1.0f};  // error:/parse error
    case LineKind::Warning:
        return {0.93f, 0.77f, 0.42f, 1.0f};  // warning:
    case LineKind::Info:
        return t.primary;                     // ectave 自身消息
    case LineKind::Dim:
        return components::theme::withOpacity(t.text, 0.42f);
    case LineKind::Output:
        break;
    }
    return components::theme::withOpacity(t.text, 0.92f);
}

inline eui::Color dimText(const components::theme::ThemeColorTokens& t) {
    return components::theme::withOpacity(t.text, 0.5f);
}

inline core::Transition fastTransition() {
    return core::Transition::make(0.16f, core::Ease::OutCubic);
}

// 连接状态：工具栏圆点色 + 文案。
enum class ConnDot { Idle, Ready, Busy, Failed };

inline ConnDot connectionState(const OctaveEngine& engine) {
    if (g_outstandingCommands > 0) {
        return ConnDot::Busy;
    }
    switch (engine.state()) {
    case EngineState::Ready:
        return ConnDot::Ready;
    case EngineState::Starting:
        return ConnDot::Busy;
    case EngineState::Failed:
        return ConnDot::Failed;
    case EngineState::Stopped:
        break;
    }
    return ConnDot::Idle;
}

inline std::string connectionLabel(const OctaveEngine& engine) {
    if (g_outstandingCommands > 0) {
        return "执行中…";
    }
    switch (engine.state()) {
    case EngineState::Ready:
        return "Octave " + (g_octaveVersion.empty() ? "?" : g_octaveVersion);
    case EngineState::Starting:
        return "启动中…";
    case EngineState::Failed:
        return "启动失败";
    case EngineState::Stopped:
        break;
    }
    return "未连接";
}

inline eui::Color connectionDotColor(ConnDot dot, const components::theme::ThemeColorTokens& t) {
    switch (dot) {
    case ConnDot::Ready:
        return {0.36f, 0.78f, 0.48f, 1.0f};
    case ConnDot::Busy:
        return {0.95f, 0.75f, 0.35f, 1.0f};
    case ConnDot::Failed:
        return {0.90f, 0.42f, 0.38f, 1.0f};
    case ConnDot::Idle:
        break;
    }
    return components::theme::withOpacity(t.text, 0.35f);
}

} // namespace ectave::ui
