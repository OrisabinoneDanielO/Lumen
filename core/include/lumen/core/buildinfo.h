#pragma once

#include <QString>

namespace lumen::core {

// Human-readable product name, used for window titles and the About screen.
QString appName();

// Organisation name used for QSettings and data folders.
QString organisationName();

// Semantic version from the top-level CMake project() call.
QString version();

} // namespace lumen::core
