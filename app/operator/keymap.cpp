#include "keymap.h"

namespace lumen::app {

KeyMap::KeyMap()
{
    m_bindings = {
        {Qt::Key_Space, KeyAction::NextSlide, {}, 0},
        {Qt::Key_Right, KeyAction::NextSlide, {}, 0},
        {Qt::Key_Left, KeyAction::PreviousSlide, {}, 0},
        {Qt::Key_PageDown, KeyAction::NextItem, {}, 0},
        {Qt::Key_PageUp, KeyAction::PreviousItem, {}, 0},
        {Qt::Key_F1, KeyAction::ClearBackground, {}, 0},
        {Qt::Key_F2, KeyAction::ClearSlide, {}, 0},
        {Qt::Key_F3, KeyAction::ClearProps, {}, 0},
        {Qt::Key_F4, KeyAction::ClearMessages, {}, 0},
        {Qt::Key_F5, KeyAction::ClearAll, {}, 0},
        {Qt::Key_F6, KeyAction::Logo, {}, 0},
        {Qt::Key_F7, KeyAction::Black, {}, 0},
        {Qt::Key_C, KeyAction::JumpToGroup, QStringLiteral("Chorus"), 0},
        {Qt::Key_B, KeyAction::JumpToGroup, QStringLiteral("Bridge"), 0},
        {Qt::Key_P, KeyAction::JumpToGroup, QStringLiteral("Pre-chorus"), 0},
        {Qt::Key_E, KeyAction::JumpToGroup, QStringLiteral("Ending"), 0},
        {Qt::Key_V, KeyAction::VersePrefix, {}, 0},
    };
    for (int n = 1; n <= 9; ++n)
        m_bindings.push_back({Qt::Key_0 + n, KeyAction::JumpToSection, {}, n});
}

std::optional<KeyBinding> KeyMap::lookup(int key, Qt::KeyboardModifiers modifiers) const
{
    // Number keys on the keypad count the same; Shift, Ctrl, Alt and Meta
    // combinations are not live shortcuts.
    if ((modifiers & ~Qt::KeypadModifier) != Qt::NoModifier)
        return std::nullopt;
    for (const KeyBinding &binding : m_bindings) {
        if (binding.key == key)
            return binding;
    }
    return std::nullopt;
}

} // namespace lumen::app
