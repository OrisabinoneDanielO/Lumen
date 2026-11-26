#include "stagecontroller.h"

#include "operatorcontroller.h"
#include "timercontroller.h"

namespace lumen::app {

StageController::StageController(OperatorController &op, TimerController &timers, QObject *parent)
    : QObject(parent), m_op(op), m_timers(timers)
{
    // Built in (a layout editor arrives with the full editor, batch 16).
    m_layouts.setRows({
        {{"name", tr("Default")}, {"key", "default"},
         {"description", tr("Now and next, arrangement, timer, clock and message")}},
        {{"name", tr("Lyrics")}, {"key", "lyrics"}, {"description", tr("Now and next, large, for singers")}},
        {{"name", tr("Clock")}, {"key", "clock"}, {"description", tr("Time and timer, large, for the speaker")}},
    });
    connect(&m_op, &OperatorController::stateChanged, this, &StageController::push);
    connect(&m_timers, &TimerController::statesChanged, this, &StageController::push);
}

void StageController::setLayout(int index)
{
    if (index < 0 || index >= m_layouts.rowCount() || index == m_layout)
        return;
    m_layout = index;
    emit changed();
    push();
}

bool StageController::showMessage(const QString &text)
{
    const QString line = text.simplified();
    if (line.isEmpty())
        return false;
    m_message = line;
    emit changed();
    push();
    return true;
}

void StageController::clearMessage()
{
    m_message.clear();
    emit changed();
    push();
}

void StageController::setSender(std::function<void(const QJsonObject &)> sender)
{
    m_sender = std::move(sender);
    m_sent = {};
    push();
}

void StageController::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    if (connected)
        m_sent = {}; // a new stage renderer gets the whole state
    emit changed();
    push();
}

QJsonObject StageController::state() const
{
    QJsonObject state = m_op.stageState();
    state.insert("layout", m_layouts.get(m_layout).value("key").toString());
    if (!m_message.isEmpty())
        state.insert("message", m_message);
    if (const auto timer = m_timers.stageTimer())
        state.insert("timer", *timer);
    return state;
}

// Sends the state when it differs from what the stage shows.
void StageController::push()
{
    if (!m_sender || !m_connected)
        return;
    const QJsonObject now = state();
    if (now == m_sent)
        return;
    m_sent = now;
    m_sender(now);
}

} // namespace lumen::app
