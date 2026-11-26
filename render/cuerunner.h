#pragma once

#include "lumen/core/show/scene.h"
#include "lumen/core/show/timecode.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <functional>
#include <memory>
#include <vector>

class QAudioOutput;
class QMediaPlayer;

namespace lumen::core::midi {
class MtcInput;
}

namespace lumen::render {

// Plays a backing track and says which cue is due (batch 20), for one layer
// showing cued content. The timeline's clock is the render clock, kept on
// the audio: it waits for the track to start, then follows the player's
// position (small differences are eased out, large ones jumped), so cues
// change on the first frame at or after their time. Without a track (or
// with no audio device) the render clock alone keeps time.
//
// With "source": "mtc" the timeline follows MIDI timecode from another
// device instead: cue time 0 is the timecode "mtcStart" (for example
// 01:00:00:00), the clock runs while the timecode runs and stops when it
// stops, and a jump in the timecode (a locate) may take cues back.
//
// "delayMs": how long the sound system takes to bring the track to the
// audience (a digital mixer, wireless in-ears); the cues follow the track as
// heard, that much after Lumen plays it.
class CueRunner : public QObject
{
    Q_OBJECT

public:
    // clock: the render clock (ms), read when the audio reports its position.
    CueRunner(const QJsonObject &timeline, std::function<qint64()> clock, QObject *parent = nullptr);
    ~CueRunner() override;

    const QJsonObject &timeline() const { return m_timeline; }
    // Same id, play state, offset, delay and source: the same run (nothing
    // to restart).
    bool sameRun(const QJsonObject &timeline) const;

    // Timeline position at render-clock time nowMs.
    qint64 positionMs(qint64 nowMs) const;
    // The cue due at a position (-1 before the first).
    int cueAt(qint64 positionMs) const;
    core::show::Content content(int cue) const;
    qint64 cueMs(int cue) const;
    int cueCount() const { return int(m_cues.size()); }

    // "none" (no track), "waiting", "playing", "paused", "unavailable";
    // following timecode: "timecode-waiting", "timecode", "timecode-stopped".
    QString audioState() const { return m_audioState; }
    qint64 audioPositionMs() const;

    // Following timecode: one MIDI message received at render-clock nowMs
    // (from MtcInput, or a test).
    void feedMidi(const QByteArray &bytes, qint64 nowMs);
    bool followsTimecode() const { return m_timecode; }
    // The timecode last received, as hh:mm:ss:ff (empty before any).
    QString timecodeText(qint64 nowMs) const;

    // Called each frame: follows the audio; true if the due cue changed
    // (*cue set).
    bool update(qint64 nowMs, int *cue);

    // When each cue came up: {cue, atMs} with the timeline position of the
    // frame it was shown on (the last 50).
    QJsonArray switches() const { return m_switches; }
    // The largest difference between the clock and the audio seen after the
    // first second of playing (ms), and the audio's reporting step.
    qint64 worstSyncMs() const { return m_worstSync; }
    qint64 audioStepMs() const { return m_stepMs; }

private:
    void anchor(qint64 positionMs, qint64 nowMs);

public:
    // The player reported this position (tests call it with "testFeed").
    void onAudioPosition(qint64 audioMs);

private:

    QJsonObject m_timeline;
    std::vector<std::pair<qint64, core::show::Content>> m_cues;
    bool m_playing = false;
    qint64 m_offsetMs = 0;
    qint64 m_delayMs = 0;
    // position = m_anchorPos + (now - m_anchorNow) while running.
    qint64 m_anchorPos = 0;
    qint64 m_anchorNow = 0;
    bool m_running = false; // the clock moves (playing, and the audio has started)
    qint64 m_startNow = 0;
    int m_cue = -2;
    std::unique_ptr<QMediaPlayer> m_player;
    std::unique_ptr<QAudioOutput> m_output;
    QString m_audioState = QStringLiteral("none");
    qint64 m_lastNow = 0;
    QJsonArray m_switches;
    std::function<qint64()> m_clock;
    bool m_startPending = false;
    qint64 m_lastReport = -1;
    qint64 m_stepMs = 85;     // how far apart the audio's reports are
    qint64 m_runningSince = 0;
    qint64 m_worstSync = 0;
    bool m_jumped = false;    // a stall or seek: cues may go back
    // Following MIDI timecode.
    bool m_timecode = false;
    QString m_mtcStart;
    core::show::MtcDecoder m_mtc;
    std::unique_ptr<core::midi::MtcInput> m_mtcInput;
};

} // namespace lumen::render
