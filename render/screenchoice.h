#pragma once

#include <optional>

namespace lumen::render {

struct ScreenChoice
{
    int index = -1;          // into QGuiApplication::screens(); -1: no screen
    bool fullscreen = false; // false: an ordinary window the operator can move

    bool operator==(const ScreenChoice &) const = default;
};

// Picks where the output window goes.
// - A valid requested screen is used fullscreen, as asked.
// - Otherwise the first screen that is not the operator's (primary) screen.
// - With only one screen, a window on it instead, so the output never covers
//   the operator UI.
ScreenChoice chooseOutputScreen(int screenCount, int primaryIndex, std::optional<int> requested);

} // namespace lumen::render
