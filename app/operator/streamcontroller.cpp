#include "streamcontroller.h"

namespace lumen::app {

StreamController::StreamController(bool ndiAvailable, QObject *parent) : QObject(parent), m_ndiAvailable(ndiAvailable) {}

void StreamController::setKeyFill(bool on)
{
    if (on == m_keyFill)
        return;
    m_keyFill = on;
    choiceChanged();
}

void StreamController::setNdi(bool on)
{
    if (on == m_ndi)
        return;
    m_ndi = on;
    choiceChanged();
}

void StreamController::setNdiName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == m_ndiName)
        return;
    m_ndiName = trimmed;
    choiceChanged();
}

void StreamController::choiceChanged()
{
    emit changed();
    if (m_connected)
        emit restartRequested();
}

void StreamController::setConnected(bool connected, const QString &status)
{
    m_connected = connected;
    m_status = status;
    emit changed();
}

QStringList StreamController::renderArguments() const
{
    QStringList arguments{QStringLiteral("--stream")};
    if (m_keyFill)
        arguments << QStringLiteral("--key-fill");
    if (ndi())
        arguments << QStringLiteral("--ndi=") + m_ndiName;
    return arguments;
}

} // namespace lumen::app
