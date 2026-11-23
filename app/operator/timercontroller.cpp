#include "timercontroller.h"

#include "operatorcontroller.h"

#include "lumen/core/data/repository.h"
#include "lumen/core/show/showcontroller.h"

#include <QDateTime>
#include <QLocale>

namespace lumen::app {

using namespace core::data;
using core::show::Content;
using core::show::Layer;

namespace {

QString modeName(TimerMode mode)
{
    switch (mode) {
    case TimerMode::Countdown: return QStringLiteral("countdown");
    case TimerMode::CountdownToTime: return QStringLiteral("countdownToTime");
    case TimerMode::Elapsed: return QStringLiteral("elapsed");
    }
    return {};
}

QString clockText(qint64 ms)
{
    return core::show::timerText(QJsonObject{{"mode", "countdown"}, {"pausedMs", ms}}, 0);
}

} // namespace

TimerController::TimerController(OperatorController &op, core::show::ShowController &show, QObject *parent)
    : QObject(parent), m_op(op), m_show(show), m_now([] { return QDateTime::currentMSecsSinceEpoch(); })
{
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, &TimerController::refresh);
    m_tick.start();
}

void TimerController::attach(Database &db)
{
    m_db = &db;
    auto existing = Repository<Timer>(db).list();
    if (existing.ok() && existing.value().empty()) {
        // Three to start with; the operator can add more.
        (void)db.inTransaction([&]() -> core::Status {
            for (const Timer &t : {Timer{.name = tr("Service start"), .mode = TimerMode::CountdownToTime, .targetTime = QTime(10, 0)},
                                   Timer{.name = tr("Sermon"), .mode = TimerMode::Countdown, .durationMs = 30 * 60 * 1000},
                                   Timer{.name = tr("Elapsed"), .mode = TimerMode::Elapsed}}) {
                if (auto created = Repository<Timer>(db).create(t); !created.ok())
                    return created.error();
            }
            return core::success();
        });
    }
    load();
    m_op.setTimerSource([this](const QString &name) { return spec(name); });
}

void TimerController::load()
{
    m_timers.clear();
    if (m_db) {
        if (auto all = Repository<Timer>(*m_db).list(); all.ok())
            m_timers = all.value();
    }
    for (const Timer &t : m_timers) {
        if (!m_states.contains(t.id))
            m_states.insert(t.id, initialState(t));
    }
    refresh();
}

TimerController::State TimerController::initialState(const Timer &timer) const
{
    State state;
    if (timer.mode == TimerMode::Countdown)
        state.pausedMs = timer.durationMs;
    return state;
}

QString TimerController::idAt(int row) const
{
    return row >= 0 && row < int(m_timers.size()) ? m_timers[size_t(row)].id : QString();
}

QJsonObject TimerController::specFor(const QString &id) const
{
    const auto timer = std::find_if(m_timers.begin(), m_timers.end(), [&](const Timer &t) { return t.id == id; });
    if (timer == m_timers.end())
        return {};
    QJsonObject spec{{"id", id}, {"mode", modeName(timer->mode)}};
    if (timer->mode == TimerMode::CountdownToTime) {
        // Today's time of day, in local time: always counting.
        const QTime target = timer->targetTime.value_or(QTime(0, 0));
        spec.insert("endsAtMs", QDateTime(QDate::currentDate(), target).toMSecsSinceEpoch());
        return spec;
    }
    const State state = m_states.value(id);
    if (!state.running)
        spec.insert("pausedMs", state.pausedMs);
    else if (timer->mode == TimerMode::Countdown)
        spec.insert("endsAtMs", state.endsAtMs);
    else
        spec.insert("startedAtMs", state.startedAtMs);
    return spec;
}

std::optional<QJsonObject> TimerController::spec(const QString &name) const
{
    for (const Timer &t : m_timers) {
        if (!name.isEmpty() && t.name.compare(name, Qt::CaseInsensitive) == 0)
            return specFor(t.id);
    }
    for (const Timer &t : m_timers) {
        if (t.mode == TimerMode::CountdownToTime)
            return specFor(t.id);
    }
    return m_timers.empty() ? std::nullopt : std::optional<QJsonObject>(specFor(m_timers.front().id));
}

bool TimerController::start(int row)
{
    const QString id = idAt(row);
    const Timer &timer = m_timers[size_t(std::max(row, 0))];
    if (id.isEmpty() || timer.mode == TimerMode::CountdownToTime || m_states[id].running)
        return false;
    State &state = m_states[id];
    const qint64 now = m_now();
    state.running = true;
    if (timer.mode == TimerMode::Countdown)
        state.endsAtMs = now + state.pausedMs;
    else
        state.startedAtMs = now - state.pausedMs;
    updateOnScreen(id);
    return true;
}

bool TimerController::pause(int row)
{
    const QString id = idAt(row);
    if (id.isEmpty() || !m_states[id].running)
        return false;
    State &state = m_states[id];
    const qint64 now = m_now();
    state.running = false;
    state.pausedMs = m_timers[size_t(row)].mode == TimerMode::Countdown ? std::max<qint64>(0, state.endsAtMs - now)
                                                                         : now - state.startedAtMs;
    updateOnScreen(id);
    return true;
}

bool TimerController::reset(int row)
{
    const QString id = idAt(row);
    if (id.isEmpty())
        return false;
    m_states[id] = initialState(m_timers[size_t(row)]);
    updateOnScreen(id);
    return true;
}

bool TimerController::show(int row)
{
    const QString id = idAt(row);
    if (id.isEmpty())
        return false;
    m_show.setContent(Layer::Messages, Content::timerContent(specFor(id), m_timers[size_t(row)].name));
    refresh();
    emit statesChanged();
    return true;
}

void TimerController::hide()
{
    if (m_show.scene().layer(Layer::Messages).content.kind == Content::Kind::Timer)
        m_show.setContent(Layer::Messages, Content::empty());
    refresh();
    emit statesChanged();
}

bool TimerController::add(const QString &name, const QString &mode, const QString &value)
{
    if (!m_db || name.trimmed().isEmpty())
        return false;
    Timer timer{.name = name.trimmed()};
    if (mode == QLatin1String("countdown")) {
        bool ok = false;
        const double minutes = value.toDouble(&ok);
        if (!ok || minutes <= 0 || minutes > 24 * 60)
            return false;
        timer.mode = TimerMode::Countdown;
        timer.durationMs = qint64(minutes * 60000);
    } else if (mode == QLatin1String("countdownToTime")) {
        const QTime time = QTime::fromString(value.trimmed(), QStringLiteral("H:mm"));
        if (!time.isValid())
            return false;
        timer.mode = TimerMode::CountdownToTime;
        timer.targetTime = time;
    } else if (mode == QLatin1String("elapsed")) {
        timer.mode = TimerMode::Elapsed;
    } else {
        return false;
    }
    if (!Repository<Timer>(*m_db).create(timer).ok())
        return false;
    load();
    emit statesChanged();
    return true;
}

bool TimerController::remove(int row)
{
    const QString id = idAt(row);
    if (!m_db || id.isEmpty() || !Repository<Timer>(*m_db).remove(id).ok())
        return false;
    // Off the screen too.
    for (Layer layer : {Layer::Messages, Layer::Slide}) {
        const Content &content = m_show.scene().layer(layer).content;
        if (content.kind == Content::Kind::Timer && content.timer.value("id").toString() == id)
            m_show.setContent(layer, Content::empty());
    }
    m_states.remove(id);
    load();
    emit statesChanged();
    return true;
}

// A timer on screen (banner or countdown slide) changes there at once.
std::optional<QJsonObject> TimerController::stageTimer() const
{
    const Content &banner = m_show.scene().layer(Layer::Messages).content;
    const QString shown = banner.kind == Content::Kind::Timer ? banner.timer.value("id").toString() : QString();
    const Timer *pick = nullptr;
    for (const Timer &t : m_timers)
        pick = !pick && t.id == shown ? &t : pick;
    for (const Timer &t : m_timers)
        pick = !pick && t.mode != TimerMode::CountdownToTime && m_states.value(t.id).running ? &t : pick;
    for (const Timer &t : m_timers)
        pick = !pick && t.mode == TimerMode::CountdownToTime ? &t : pick;
    if (!pick)
        return std::nullopt;
    return QJsonObject{{"label", pick->name}, {"spec", specFor(pick->id)}};
}

void TimerController::updateOnScreen(const QString &id)
{
    for (Layer layer : {Layer::Messages, Layer::Slide}) {
        const Content &content = m_show.scene().layer(layer).content;
        if (content.kind == Content::Kind::Timer && content.timer.value("id").toString() == id)
            m_show.setContent(layer, Content::timerContent(specFor(id), content.text));
    }
    refresh();
    emit statesChanged();
}

void TimerController::refresh()
{
    const qint64 now = m_now();
    const Content &banner = m_show.scene().layer(Layer::Messages).content;
    const QString shown = banner.kind == Content::Kind::Timer ? banner.timer.value("id").toString() : QString();
    QList<QVariantMap> rows;
    QString summary;
    for (const Timer &t : m_timers) {
        const QJsonObject spec = specFor(t.id);
        const bool running = !spec.contains("pausedMs");
        QString setting;
        if (t.mode == TimerMode::Countdown)
            setting = clockText(t.durationMs);
        else if (t.mode == TimerMode::CountdownToTime)
            setting = tr("to %1").arg(QLocale().toString(t.targetTime.value_or(QTime(0, 0)), QLocale::ShortFormat));
        else
            setting = tr("count up");
        const QString display = core::show::timerText(spec, now);
        rows << QVariantMap{{"name", t.name}, {"mode", modeName(t.mode)}, {"setting", setting},
                            {"display", display}, {"running", running}, {"shown", t.id == shown}};
        // The toolbar shows the first started timer (countdowns to a time
        // always count, so they show only if nothing else runs).
        if (summary.isEmpty() && running && t.mode != TimerMode::CountdownToTime)
            summary = QStringLiteral("%1 %2").arg(t.name, display);
    }
    m_model.updateRows(rows);
    if (summary != m_summary) {
        m_summary = summary;
        emit changed();
    }
}

} // namespace lumen::app
