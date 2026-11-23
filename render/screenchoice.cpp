#include "screenchoice.h"

namespace lumen::render {

ScreenChoice chooseOutputScreen(int screenCount, int primaryIndex, std::optional<int> requested)
{
    if (screenCount <= 0)
        return {};
    if (requested && *requested >= 0 && *requested < screenCount)
        return {*requested, true};
    if (primaryIndex < 0 || primaryIndex >= screenCount)
        primaryIndex = 0;
    for (int i = 0; i < screenCount; ++i) {
        if (i != primaryIndex)
            return {i, true};
    }
    return {primaryIndex, false};
}

} // namespace lumen::render
