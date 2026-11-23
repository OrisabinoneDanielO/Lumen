#pragma once

#include <QtGlobal>

namespace lumen::render {

// True while process `pid` is running (used by --watch-pid in tests).
bool processRunning(qint64 pid);

} // namespace lumen::render
