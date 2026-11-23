#pragma once

#include <QString>

#include <Qt>
#include <optional>
#include <vector>

namespace lumen::app {

// Live actions a key can trigger (design doc: "Keyboard map").
enum class KeyAction {
    NextSlide,
    PreviousSlide,
    NextItem,
    PreviousItem,
    ClearBackground,
    ClearSlide,
    ClearProps,
    ClearMessages,
    ClearAll,
    Logo,
    Black,
    JumpToGroup,   // argument: label prefix, e.g. "Chorus"
    VersePrefix,   // V: the next digit picks a verse
    JumpToSection, // argument: 1-based section number
};

struct KeyBinding
{
    int key = 0;          // Qt::Key
    KeyAction action = KeyAction::NextSlide;
    QString group;        // JumpToGroup
    int section = 0;      // JumpToSection
};

// The default bindings, as data, so they can be made remappable later. Only
// unmodified keys are bound, so Ctrl/Alt shortcuts stay free.
class KeyMap
{
public:
    KeyMap();

    std::optional<KeyBinding> lookup(int key, Qt::KeyboardModifiers modifiers) const;
    const std::vector<KeyBinding> &bindings() const { return m_bindings; }

private:
    std::vector<KeyBinding> m_bindings;
};

} // namespace lumen::app
