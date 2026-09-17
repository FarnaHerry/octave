// ui/command_input.cppm — 命令栏输入框：components::input 的应用内派生。
//
// 为什么复制而不是直接用 components::input：
//   1) 内置单行 input 的 Escape 键也会触发 onEnter（= 提交！），REPL 里
//      「Esc 清空草稿」被误触发成执行；
//   2) 内置 multiline input 的 Enter 永远插换行，没有「Enter 提交、
//      Shift+Enter 换行」语义；
//   3) 命令栏需要 ↑↓ 历史。
// 本派生完全复用 InputModel/InputLayout（选区、撤销、IME、剪贴板逻辑不动），
// 只重写按键分派：
//   Enter          提交（onSubmit）
//   Shift+Enter    插入换行（多行缓冲）
//   ↑ / ↓          缓冲为单行时走历史（onHistory(-1/+1)）；含换行时走光标上下移
//   Escape         清空草稿（onEscape），不提交
// 焦点热区 id 为 <id>.hit（与内置 input 一致），页面可用 ui.setFocusedId 抢焦点。
module;

#include "eui_ui.h"

export module ectave.ui.command_input;

import std;
import ectave.ui.utils;
import ectave.ui.theme;

export namespace ectave::ui {

class CommandInputBuilder {
public:
    CommandInputBuilder(core::dsl::Ui& ui, std::string id)
        : ui_(ui), id_(std::move(id)) {}

    CommandInputBuilder& size(float width, float height) { width_ = width; height_ = height; return *this; }
    CommandInputBuilder& value(std::string value) { text_ = std::move(value); return *this; }
    CommandInputBuilder& placeholder(std::string value) { placeholder_ = std::move(value); return *this; }
    CommandInputBuilder& fontSize(float value) { fontSize_ = std::max(1.0f, value); return *this; }
    CommandInputBuilder& fontFamily(std::string value) { fontFamily_ = std::move(value); return *this; }
    CommandInputBuilder& inset(float value) { inset_ = std::max(0.0f, value); return *this; }
    CommandInputBuilder& style(const components::InputStyle& value) { style_ = value; return *this; }
    CommandInputBuilder& theme(const components::theme::ThemeColorTokens& tokens) {
        style_ = components::InputStyle(tokens);
        metrics_ = tokens.metrics;
        return *this;
    }
    CommandInputBuilder& transition(const core::Transition& value) { transition_ = value; return *this; }
    CommandInputBuilder& onChange(std::function<void(const std::string&)> callback) {
        onChange_ = std::move(callback);
        return *this;
    }
    CommandInputBuilder& onSubmit(std::function<void()> callback) {
        onSubmit_ = std::move(callback);
        return *this;
    }
    CommandInputBuilder& onEscape(std::function<void()> callback) {
        onEscape_ = std::move(callback);
        return *this;
    }
    CommandInputBuilder& onHistory(std::function<void(int)> callback) {
        onHistory_ = std::move(callback);
        return *this;
    }
    CommandInputBuilder& onFocus(std::function<void(bool)> callback) {
        onFocus_ = std::move(callback);
        return *this;
    }

    void build() {
        const std::string hitId = id_ + ".hit";
        const bool focused = ui_.isFocused(hitId);
        const float inset = inset_ >= 0.0f ? inset_ : metrics_.spacing.content;
        const float fontSize = fontSize_ > 0.0f ? fontSize_ : metrics_.typography.input;
        const float textWidth = std::max(0.0f, width_ - inset * 2.0f);
        const std::function<void(const std::string&)> onChange = onChange_;
        const std::function<void()> onSubmit = onSubmit_;
        const std::function<void()> onEscape = onEscape_;
        const std::function<void(int)> onHistory = onHistory_;
        const std::function<void(bool)> onFocus = onFocus_;
        const float textLineHeight = fontSize * 1.2f;
        const float textHeight = std::max(0.0f, height_ - inset * 2.0f);
        const float width = width_;
        const std::string fontFamily = fontFamily_;  // 回调可稳定捕获的局部副本
        InputState& state = ui_.state<InputState>(id_);
        if (state.text != text_) {
            const bool wasFocused = focused;
            state.text = text_;
            ++state.textRevision;
            state.cursor = InputModel::clampUtf8Boundary(state.text, static_cast<int>(state.text.size()));
            state.selectionStart = state.cursor;
            state.selectionEnd = state.cursor;
            if (!wasFocused) {
                state.horizontalScroll = 0.0f;
                state.verticalScroll = 0.0f;
                state.undoStack.clear();
                state.redoStack.clear();
            }
        }
        state.cursor = InputModel::clampUtf8Boundary(state.text, state.cursor);
        state.selectionStart = InputModel::clampUtf8Boundary(state.text, state.selectionStart);
        state.selectionEnd = InputModel::clampUtf8Boundary(state.text, state.selectionEnd);
        const InputLayout layout = InputLayout::build(state, textWidth, textHeight, width_, inset, inset,
                                                      textLineHeight, fontFamily_, fontSize, true);
        const bool empty = state.text.empty();
        const bool hasComposition = focused && !state.compositionText.empty();
        const bool hasSelection = !layout.selectionRects.empty();
        const std::string textDirtyKey = id_ + ".text|" + std::to_string(state.textRevision) +
            "|" + std::to_string(static_cast<int>(std::lround(state.verticalScroll * 64.0f))) +
            (empty ? "|p" : "|v");
        const std::string compositionDirtyKey = id_ + ".composition|" + std::to_string(state.compositionRevision);

        // IME 预编辑框位置 + 光标点（与内置 input 同一套计算，中文输入候选窗
        // 靠 imeRect 跟随）。
        const float compositionPadding = metrics_.spacing.hairline;
        const float compositionTextLeft = inset;
        const float compositionTextRight = std::max(compositionTextLeft, width_ - inset);
        const float compositionAvailableWidth = std::max(4.0f, compositionTextRight - compositionTextLeft);
        const float compositionTextWidth = hasComposition
            ? InputModel::measureMetrics(state.compositionText, fontFamily_, fontSize).width
            : 0.0f;
        const float compositionWidth = hasComposition
            ? std::clamp(std::ceil(compositionTextWidth) + compositionPadding * 2.0f, 2.0f, compositionAvailableWidth)
            : 0.0f;
        const float compositionX = hasComposition
            ? std::clamp(layout.clampedCursorX(), compositionTextLeft,
                         std::max(compositionTextLeft, compositionTextRight - compositionWidth))
            : layout.clampedCursorX();
        const float caretX = hasComposition ? std::clamp(compositionX + compositionWidth, inset,
                                                         std::max(inset, width_ - inset))
                                            : layout.clampedCursorX();

        ui_.stack(id_)
            .size(width_, height_)
            .clip()
            .dirtyKey(InputModel::makeDirtyKey(state, focused, layout))
            .content([&] {
                auto hit = ui_.rect(hitId)
                    .size(width_, height_)
                    .color(style_.background)
                    .radius(style_.radius)
                    .border(1.0f, focused ? style_.focusBorder : style_.border)
                    .shadow(focused ? style_.shadow : core::Shadow{})
                    .transition(transition_)
                    .focusable()
                    .imeRect(hasComposition ? compositionX : caretX, layout.cursorY, 1.5f, textLineHeight)
                    .onPress([&state, width, inset, layout](const core::PointerEvent& event, const core::Rect& bounds) {
                        state.lastBounds = bounds;
                        state.cursor = InputModel::clampUtf8Boundary(
                            state.text, layout.cursorFromPointer(event.x, event.y, bounds, width, inset));
                        state.hasPreferredCursorX = false;
                        InputModel::clearSelection(state);
                        state.dragAnchor = state.cursor;
                        state.selecting = true;
                    })
                    .onFocusChanged(onFocus)
                    .onDrag([&state, width, inset, fontSize, fontFamily, textHeight, layout](const core::dsl::DragEvent& event) {
                        state.cursor = InputModel::clampUtf8Boundary(
                            state.text, layout.cursorFromPointer(event.x, event.y, state.lastBounds, width, inset));
                        state.hasPreferredCursorX = false;
                        state.selectionStart = state.dragAnchor;
                        state.selectionEnd = state.cursor;
                        InputModel::syncVerticalScroll(state, layout, textHeight);
                    });
                if (layout.maxVerticalScroll > 0.0f) {
                    hit.onScroll([&state, layout, fontSize](const core::ScrollEvent& event) {
                        const float step = std::max(12.0f, fontSize * 2.2f);
                        state.followCaret = false;
                        state.verticalScroll = std::clamp(
                            state.verticalScroll - static_cast<float>(event.y) * step,
                            0.0f,
                            layout.maxVerticalScroll);
                    });
                }
                hit.onKeyEvent([&state, onChange, onSubmit, onEscape, onHistory, layout,
                                width, inset, fontSize, fontFamily, textHeight](const core::KeyEvent& event) {
                    if (!event.isDown()) {
                        return false;
                    }

                    state.followCaret = true;
                    bool changed = false;
                    bool handled = true;
                    const bool shortcut = event.modifiers.shortcut();
                    if (!state.compositionText.empty() &&
                        (event.key == core::InputKey::Backspace ||
                         event.key == core::InputKey::Delete)) {
                        return true;
                    }
                    const bool undo = shortcut && !event.modifiers.shift && event.key == core::InputKey::Z;
                    const bool redo = shortcut &&
                        (event.key == core::InputKey::Y ||
                         (event.modifiers.shift && event.key == core::InputKey::Z));
                    if (undo || redo) {
                        if (!state.compositionText.empty()) {
                            state.compositionText.clear();
                            ++state.compositionRevision;
                        }
                        changed = undo ? InputModel::undoEdit(state) : InputModel::redoEdit(state);
                        state.horizontalScroll = 0.0f;
                        const InputLayout nextLayout = InputLayout::build(
                            state, std::max(0.0f, width - inset * 2.0f), textHeight, width, inset,
                            0.0f, fontSize, fontFamily, fontSize, true);
                        InputModel::syncVerticalScroll(state, nextLayout, textHeight);
                        if (changed && onChange) {
                            onChange(state.text);
                        }
                        return true;
                    }

                    if (shortcut && event.key == core::InputKey::A) {
                        state.selectionStart = 0;
                        state.selectionEnd = static_cast<int>(state.text.size());
                        state.cursor = state.selectionEnd;
                    } else if (shortcut && event.key == core::InputKey::C) {
                        InputModel::copySelection(state);
                    } else if (shortcut && event.key == core::InputKey::X) {
                        if (InputModel::hasTextSelection(state)) {
                            InputModel::copySelection(state);
                            InputModel::pushUndoState(state);
                            InputModel::eraseSelection(state);
                            changed = true;
                        }
                    } else if (event.key == core::InputKey::Enter) {
                        if (event.modifiers.shift) {
                            // Shift+Enter：多行缓冲里插换行（块编辑）。
                            InputModel::pushUndoState(state);
                            InputModel::insertAtCursor(state, "\n");
                            changed = true;
                        } else if (onSubmit) {
                            onSubmit();
                        }
                    } else if (event.key == core::InputKey::Escape) {
                        // 与内置 input 的关键差异：Escape 清空草稿，绝不提交。
                        if (onEscape) {
                            onEscape();
                        }
                    } else if ((event.key == core::InputKey::Up || event.key == core::InputKey::Down) &&
                               onHistory && state.text.find('\n') == std::string::npos) {
                        if (state.selectionStart != state.selectionEnd) {
                            InputModel::clearSelection(state);
                        }
                        onHistory(event.key == core::InputKey::Up ? -1 : +1);
                    } else if (event.key == core::InputKey::Left) {
                        InputModel::moveCursor(state, -1, event.modifiers.shift, fontFamily, fontSize, true,
                                               std::max(0.0f, width - inset * 2.0f));
                    } else if (event.key == core::InputKey::Right) {
                        InputModel::moveCursor(state, 1, event.modifiers.shift, fontFamily, fontSize, true,
                                               std::max(0.0f, width - inset * 2.0f));
                    } else if (event.key == core::InputKey::Up) {
                        InputModel::moveCursorVertical(state, -1, event.modifiers.shift, fontFamily, fontSize,
                                                       std::max(0.0f, width - inset * 2.0f), textHeight);
                    } else if (event.key == core::InputKey::Down) {
                        InputModel::moveCursorVertical(state, 1, event.modifiers.shift, fontFamily, fontSize,
                                                       std::max(0.0f, width - inset * 2.0f), textHeight);
                    } else if (event.key == core::InputKey::Home) {
                        InputModel::moveCursorToLineEdge(state, false, event.modifiers.shift, fontFamily, fontSize,
                                                         std::max(0.0f, width - inset * 2.0f));
                    } else if (event.key == core::InputKey::End) {
                        InputModel::moveCursorToLineEdge(state, true, event.modifiers.shift, fontFamily, fontSize,
                                                         std::max(0.0f, width - inset * 2.0f));
                    } else if (event.key == core::InputKey::Delete) {
                        if (InputModel::hasTextSelection(state)) {
                            InputModel::pushUndoState(state);
                            InputModel::eraseSelection(state);
                            changed = true;
                        } else if (state.cursor < static_cast<int>(state.text.size())) {
                            const int next = InputModel::nextCursorIndex(state, fontFamily, fontSize, true,
                                                                         std::max(0.0f, width - inset * 2.0f));
                            InputModel::pushUndoState(state);
                            state.text.erase(static_cast<std::size_t>(state.cursor),
                                             static_cast<std::size_t>(next - state.cursor));
                            ++state.textRevision;
                            changed = true;
                        }
                    } else if (event.key == core::InputKey::Backspace) {
                        if (InputModel::hasTextSelection(state)) {
                            InputModel::pushUndoState(state);
                            InputModel::eraseSelection(state);
                            changed = true;
                        } else if (state.cursor > 0) {
                            const int previous = InputModel::prevCursorIndex(state, fontFamily, fontSize, true,
                                                                             std::max(0.0f, width - inset * 2.0f));
                            InputModel::pushUndoState(state);
                            state.text.erase(static_cast<std::size_t>(previous),
                                             static_cast<std::size_t>(state.cursor - previous));
                            ++state.textRevision;
                            state.cursor = previous;
                            InputModel::clearSelection(state);
                            changed = true;
                        }
                    } else {
                        handled = false;
                    }
                    state.horizontalScroll = 0.0f;
                    InputModel::syncVerticalScroll(state, layout, textHeight);
                    if (changed && onChange) {
                        onChange(state.text);
                    }
                    return handled;
                })
                    .onTextInput([&state, onChange, layout, width, inset, fontSize, fontFamily,
                                 textHeight](const core::TextInputEvent& event) {
                        state.followCaret = true;
                        bool changed = false;
                        const std::string nextComposition = event.composing
                            ? InputModel::filteredText(event.compositionText, true)
                            : std::string{};
                        if (state.compositionText != nextComposition) {
                            state.compositionText = nextComposition;
                            ++state.compositionRevision;
                        }

                        const auto insertText = [&](const std::string& text) {
                            if (text.empty()) {
                                return;
                            }
                            if (!state.compositionText.empty()) {
                                state.compositionText.clear();
                                ++state.compositionRevision;
                            }
                            InputModel::pushUndoState(state);
                            InputModel::insertAtCursor(state, InputModel::filteredText(text, true));
                            changed = true;
                        };
                        insertText(event.text);
                        insertText(event.pasteText);

                        state.horizontalScroll = 0.0f;
                        InputModel::syncVerticalScroll(state, layout, textHeight);
                        if (changed && onChange) {
                            onChange(state.text);
                        }
                    })
                    .build();

                ui_.stack(id_ + ".textViewport")
                    .position(inset, inset)
                    .size(textWidth, textHeight)
                    .clip()
                    .content([&] {
                        if (hasSelection) {
                            for (size_t index = 0; index < layout.selectionRects.size(); ++index) {
                                const auto& selectionRect = layout.selectionRects[index];
                                ui_.rect(id_ + ".selection." + std::to_string(index))
                                    .position(selectionRect.x - inset, selectionRect.y - inset)
                                    .size(selectionRect.width, selectionRect.height)
                                    .color(components::theme::withAlpha(style_.cursor, 0.24f))
                                    .build();
                            }
                        }

                        if (!empty) {
                            const auto& lines = layout.lineList();
                            for (std::size_t index = 0; index < lines.size(); ++index) {
                                const auto& line = lines[index];
                                const float y = static_cast<float>(index) * textLineHeight - state.verticalScroll;
                                if (y + textLineHeight < 0.0f || y > textHeight) {
                                    continue;
                                }
                                ui_.text(id_ + ".text." + std::to_string(index))
                                    .position(0.0f, y)
                                    .size(layout.visibleTextWidth, textLineHeight)
                                    .dirtyKey(textDirtyKey + "|" + std::to_string(index))
                                    .text(state.text.substr(static_cast<std::size_t>(line.start),
                                                            static_cast<std::size_t>(std::max(0, line.end - line.start))))
                                    .fontSize(fontSize)
                                    .fontFamily(fontFamily_)
                                    .lineHeight(textLineHeight)
                                    .color(style_.text)
                                    .wrap(false)
                                    .verticalAlign(core::VerticalAlign::Top)
                                    .build();
                            }
                        } else {
                            ui_.text(id_ + ".text")
                                .position(0.0f, 0.0f)
                                .size(layout.visibleTextWidth, textHeight)
                                .dirtyKey(textDirtyKey)
                                .text(placeholder_)
                                .fontSize(fontSize)
                                .fontFamily(fontFamily_)
                                .lineHeight(textLineHeight)
                                .color(style_.placeholder)
                                .wrap(false)
                                .verticalAlign(core::VerticalAlign::Top)
                                .build();
                        }

                        if (hasComposition) {
                            ui_.rect(id_ + ".composition.bg")
                                .position(compositionX - inset, layout.cursorY - inset)
                                .size(compositionWidth, textLineHeight)
                                .color(components::theme::withAlpha(style_.focused, 0.82f))
                                .radius(2.0f)
                                .build();

                            ui_.text(id_ + ".composition")
                                .position(compositionX + compositionPadding - inset, layout.cursorY - inset)
                                .size(std::max(1.0f, compositionWidth - compositionPadding * 2.0f), textLineHeight)
                                .dirtyKey(compositionDirtyKey)
                                .text(state.compositionText)
                                .fontSize(fontSize)
                                .fontFamily(fontFamily_)
                                .lineHeight(textLineHeight)
                                .color(style_.text)
                                .wrap(false)
                                .verticalAlign(core::VerticalAlign::Top)
                                .build();
                        }

                        if (focused) {
                            ui_.rect(id_ + ".cursor")
                                .position(caretX - inset, layout.cursorY - inset)
                                .size(1.5f, fontSize * 1.18f)
                                .color(style_.cursor)
                                .radius(1.0f)
                                .build();
                        }
                    })
                    .build();
            })
            .build();
    }

private:
    using InputModel = components::input_detail::InputModel;
    using InputState = InputModel::InputState;
    using InputLayout = InputModel::InputLayout;

    core::dsl::Ui& ui_;
    std::string id_;
    components::InputStyle style_;
    components::theme::ThemeMetricTokens metrics_;
    core::Transition transition_ = core::Transition::make(0.16f, core::Ease::OutCubic);
    std::function<void(const std::string&)> onChange_;
    std::function<void()> onSubmit_;
    std::function<void()> onEscape_;
    std::function<void(int)> onHistory_;
    std::function<void(bool)> onFocus_;
    std::string text_;
    std::string placeholder_ = "Octave 命令";
    float width_ = 400.0f;
    float height_ = 40.0f;
    float inset_ = -1.0f;
    float fontSize_ = 0.0f;
    std::string fontFamily_ = "Microsoft YaHei";
};

inline CommandInputBuilder commandInput(core::dsl::Ui& ui, const std::string& id) {
    return CommandInputBuilder(ui, id);
}

} // namespace ectave::ui
