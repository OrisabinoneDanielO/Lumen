#pragma once

#include "rowmodel.h"

#include "lumen/core/data/database.h"
#include "lumen/core/data/entities.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <functional>
#include <optional>

namespace lumen::core::show {
class ShowController;
}

namespace lumen::app {

class OperatorController;

// Timers (batch 12): countdowns for a set time ("Sermon", 30:00), countdowns
// to a time of day ("Service start", 10:00) and count-up clocks. The timers
// themselves are stored in the library; whether one is running is kept here
// and sent to the output as wall-clock times, so the render process counts
// on its own and a timer keeps time even if this UI stops.
//
// A timer can be shown on the stage display (never the audience screen:
// sermon and service timers are for the speaker and band), and slides with
// a timer element (countdown slides) show one to the audience. Starting,
// pausing or resetting a timer that is shown updates it there at once.
class TimerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *timers READ timers CONSTANT)
    Q_PROPERTY(QString summary READ summary NOTIFY changed) // toolbar: the first running timer

public:
    TimerController(OperatorController &op, core::show::ShowController &show, QObject *parent = nullptr);

    // Loads the library's timers (creating the three defaults if it has none)
    // and lets countdown slides find their timer.
    void attach(core::data::Database &db);

    QObject *timers() { return &m_model; }
    QString summary() const { return m_summary; }

    Q_INVOKABLE bool start(int row);
    Q_INVOKABLE bool pause(int row);
    Q_INVOKABLE bool reset(int row);
    Q_INVOKABLE bool show(int row);  // on the stage display
    Q_INVOKABLE void hide();         // takes it off the stage display
    // minutes for a countdown, "HH:mm" for a countdown to a time, ignored for
    // a count-up clock. Returns false (and adds nothing) if they make no sense.
    Q_INVOKABLE bool add(const QString &name, const QString &mode, const QString &value);
    Q_INVOKABLE bool remove(int row);

    // What the output needs to show the timer with this name (the first
    // countdown to a time if the name is empty or unknown).
    std::optional<QJsonObject> spec(const QString &name) const;
    QJsonObject specFor(const QString &id) const;
    // The timer for the stage display: the one in the banner, else the first
    // started countdown or clock, else the first countdown to a time.
    // {"label": name, "spec": ...}, or nothing if there are no timers.
    std::optional<QJsonObject> stageTimer() const;

    // Test support: the wall clock (ms since 1970 UTC) used for new states.
    void setClock(std::function<qint64()> clock) { m_now = std::move(clock); }

signals:
    void changed();
    void statesChanged(); // a timer started, paused, reset, was shown, added or removed

private:
    struct State
    {
        bool running = false;
        qint64 endsAtMs = 0;   // countdowns while running
        qint64 startedAtMs = 0; // count-up while running
        qint64 pausedMs = 0;   // shown while stopped
    };
    void load();
    void refresh();         // the model's display column and the summary
    void updateOnScreen(const QString &id);
    QString idAt(int row) const;
    State initialState(const core::data::Timer &timer) const;

    OperatorController &m_op;
    core::show::ShowController &m_show;
    core::data::Database *m_db = nullptr;
    std::vector<core::data::Timer> m_timers;
    QHash<QString, State> m_states;
    RowModel m_model{{"name", "mode", "setting", "display", "running", "shown"}};
    QString m_shownId; // the timer shown on the stage display, if any
    QString m_summary;
    QTimer m_tick;
    std::function<qint64()> m_now;
};

} // namespace lumen::app
