#include "lumen/core/buildinfo.h"

#ifndef LUMEN_VERSION
#error "LUMEN_VERSION must be defined by the build system"
#endif

namespace lumen::core {

QString appName()
{
    return QStringLiteral("Lumen Presenter");
}

QString organisationName()
{
    return QStringLiteral("Lumen");
}

QString version()
{
    return QStringLiteral(LUMEN_VERSION);
}

} // namespace lumen::core
