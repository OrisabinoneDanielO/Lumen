#pragma once

#include "rowmodel.h"

#include <QJsonObject>
#include <QObject>

#include <functional>

namespace lumen::app {

class OperatorController;
class TimerController;

// The stage display (batch 13): what the musicians and the speaker see on a
// confidence monitor. It runs in a render process of its own
// (lumen-render --stage), so it can never disturb the audience output.
//
// This keeps the stage state (see protocol.h) up to date from the operator
// window and the timers, adds the chosen layout and the message to the stage,
// and sends it whenever it changes. The clock and timer are counted by the
// stage renderer.
class StageController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *layouts READ layouts CONSTANT)
    Q_PROPERTY(int layout READ layout WRITE setLayout NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)

public:
    StageController(OperatorController &op, TimerController &timers, QObject *parent = nullptr);

    QObject *layouts() { return &m_layouts; }
    int layout() const { return m_layout; }
    void setLayout(int index);
    QString message() const { return m_message; }
    bool connected() const { return m_connected; }

    // A note to the stage only ("Repeat the last verse"); the audience
    // never sees it.
    Q_INVOKABLE bool showMessage(const QString &text);
    Q_INVOKABLE void clearMessage();
    // The toolbar's Stage light: asks for the stage output on or off.
    Q_INVOKABLE void toggle() { emit toggleRequested(!m_connected); }

    // Where the state goes (the stage renderer's link), and whether it is up.
    void setSender(std::function<void(const QJsonObject &)> sender);
    void setConnected(bool connected);

    QJsonObject state() const;

signals:
    void changed();
    void toggleRequested(bool on);

private:
    void push();

    OperatorController &m_op;
    TimerController &m_timers;
    RowModel m_layouts{{"name", "key", "description"}};
    int m_layout = 0;
    QString m_message;
    bool m_connected = false;
    std::function<void(const QJsonObject &)> m_sender;
    QJsonObject m_sent;
};

} // namespace lumen::app
