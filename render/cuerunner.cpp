#include "cuerunner.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>

#include <algorithm>

namespace lumen::render {

using core::show::Content;

namespace {
constexpr qint64 audioWaitMs = 2000; // no sound by then: the render clock keeps time
constexpr qint64 jumpMs = 250;       // a bigger difference from the audio is a stall or a seek
} // namespace

// How the clock keeps to the audio: the player reports its position in steps
// (one audio buffer, about 85 ms), each the end of the buffer just handed to
// the sound card, so at a report the sound is somewhere in the step before
// it. The clock is the render clock, started when playing starts, and eased
// towards the middle of each step; only a stall or a seek makes it jump.
CueRunner::CueRunner(const QJsonObject &timeline, std::function<qint64()> clock, QObject *parent)
    : QObject(parent), m_timeline(timeline), m_playing(timeline.value("playing").toBool()),
      m_offsetMs(timeline.value("offsetMs").toInteger()), m_clock(std::move(clock))
{
    const qint64 nowMs = m_clock();
    m_startNow = nowMs;
    m_lastNow = nowMs;
    for (const QJsonValue &v : timeline.value("cues").toArray()) {
        const QJsonObject cue = v.toObject();
        if (auto content = core::show::contentFromJson(cue.value("content")))
            m_cues.emplace_back(cue.value("ms").toInteger(), *content);
    }
    std::stable_sort(m_cues.begin(), m_cues.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    anchor(m_offsetMs, nowMs);

    const QString audio = timeline.value("audio").toString();
    if (audio.isEmpty()) {
        m_running = m_playing;
        return;
    }
    m_player = std::make_unique<QMediaPlayer>();
    m_output = std::make_unique<QAudioOutput>();
    m_player->setAudioOutput(m_output.get());
    m_player->setSource(QUrl::fromLocalFile(audio));
    if (!m_playing) {
        m_audioState = QStringLiteral("paused");
        return;
    }
    m_audioState = QStringLiteral("waiting");
    connect(m_player.get(), &QMediaPlayer::errorOccurred, this, [this] {
        // No audio: the render clock takes over from where it is.
        m_audioState = QStringLiteral("unavailable");
        m_running = true;
        anchor(m_offsetMs, m_clock());
    });
    connect(m_player.get(), &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState && m_audioState == QLatin1String("waiting")) {
            m_audioState = QStringLiteral("playing");
            m_running = true;
            m_runningSince = m_clock();
            anchor(m_offsetMs, m_runningSince);
        }
    });
    connect(m_player.get(), &QMediaPlayer::positionChanged, this, &CueRunner::onAudioPosition);
    if (m_offsetMs > 0)
        m_player->setPosition(m_offsetMs);
    m_player->play();
}

CueRunner::~CueRunner()
{
    if (m_player)
        m_player->stop();
}

bool CueRunner::sameRun(const QJsonObject &timeline) const
{
    return timeline.value("id") == m_timeline.value("id") && timeline.value("playing") == m_timeline.value("playing")
           && timeline.value("offsetMs") == m_timeline.value("offsetMs");
}

void CueRunner::anchor(qint64 positionMs, qint64 nowMs)
{
    m_anchorPos = positionMs;
    m_anchorNow = nowMs;
}

qint64 CueRunner::positionMs(qint64 nowMs) const
{
    return m_running ? m_anchorPos + (nowMs - m_anchorNow) : m_anchorPos;
}

qint64 CueRunner::audioPositionMs() const
{
    return m_player ? m_player->position() : -1;
}

int CueRunner::cueAt(qint64 positionMs) const
{
    int cue = -1;
    for (int i = 0; i < int(m_cues.size()) && m_cues[size_t(i)].first <= positionMs; ++i)
        cue = i;
    return cue;
}

Content CueRunner::content(int cue) const
{
    return cue >= 0 && cue < int(m_cues.size()) ? m_cues[size_t(cue)].second : Content::empty();
}

qint64 CueRunner::cueMs(int cue) const
{
    return cue >= 0 && cue < int(m_cues.size()) ? m_cues[size_t(cue)].first : -1;
}

void CueRunner::onAudioPosition(qint64 audioMs)
{
    if (m_audioState != QLatin1String("playing") || !m_running)
        return;
    const qint64 nowMs = m_clock();
    if (m_lastReport >= 0) {
        const qint64 step = audioMs - m_lastReport;
        if (step >= 10 && step <= 250)
            m_stepMs = (m_stepMs * 7 + step) / 8;
    }
    m_lastReport = audioMs;
    // The sound is in the step before the report: aim at its middle.
    const qint64 error = audioMs - m_stepMs / 2 - positionMs(nowMs);
    if (nowMs - m_runningSince > 1000)
        m_worstSync = std::max(m_worstSync, std::abs(error));
    if (std::abs(error) > jumpMs) {
        anchor(audioMs - m_stepMs / 2, nowMs);
        m_jumped = true;
    } else {
        m_anchorPos += error / 4;
    }
}

bool CueRunner::update(qint64 nowMs, int *cue)
{
    m_lastNow = nowMs;
    if (m_audioState == QLatin1String("waiting") && nowMs - m_startNow > audioWaitMs) {
        m_audioState = QStringLiteral("unavailable");
        m_running = true;
        anchor(m_offsetMs, nowMs);
    }
    int due = cueAt(positionMs(nowMs));
    // Within a run slides only go forward (easing the clock must not flip a
    // slide back), unless the audio jumped.
    if (!m_jumped && m_cue >= 0)
        due = std::max(due, m_cue);
    m_jumped = false;
    if (due == m_cue)
        return false;
    m_cue = due;
    *cue = due;
    if (due >= 0) {
        m_switches.append(QJsonObject{{"cue", due}, {"atMs", positionMs(nowMs)}, {"cueMs", cueMs(due)}});
        while (m_switches.size() > 50)
            m_switches.removeFirst();
    }
    return true;
}

} // namespace lumen::render
