#pragma once

#include "rowmodel.h"

#include "lumen/core/data/database.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

class QAudioOutput;
class QMediaPlayer;

namespace lumen::app {

class OperatorController;

// Timecode cues (batch 20): an item's slides following a backing track.
//
// An item (a song, usually) gets a backing track (an audio file) and cues:
// times in the track, each with a slide of the item in play order. They are
// kept with the presentation (metadata "timeline": {"audioAssetId", "cues":
// [{"ms", "slide"}]}).
//
// Record: the track plays here (for the operator's ears) and each slide the
// operator sends live becomes a cue at that moment. Cues can then be nudged
// a frame at a time. Play sends the whole timeline to the output, where the
// render process plays the track and changes slides on the frame.
class CueController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString itemTitle READ itemTitle NOTIFY changed)
    Q_PROPERTY(QString trackName READ trackName NOTIFY changed)
    Q_PROPERTY(QObject *cues READ cues CONSTANT)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString position READ position NOTIFY ticked)
    Q_PROPERTY(QString message READ message NOTIFY changed)

public:
    // One frame at 60 frames per second, the nudge step.
    static constexpr qint64 frameMs = 17;

    explicit CueController(OperatorController &op, QObject *parent = nullptr);
    ~CueController() override;

    void attach(core::data::Database &db);

    bool available() const { return !m_presentationId.isEmpty(); }
    QString itemTitle() const { return m_title; }
    QString trackName() const;
    QObject *cues() { return &m_model; }
    bool recording() const { return m_recording; }
    bool playing() const;
    QString position() const;
    QString message() const { return m_message; }

    Q_INVOKABLE bool setTrack(const QUrl &file);
    Q_INVOKABLE void clearTrack();
    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void nudge(int row, int frames);
    Q_INVOKABLE void removeCue(int row);
    Q_INVOKABLE bool play(); // on the output
    Q_INVOKABLE void stop();

    // The item's timeline as stored, and as the output needs it (audio path).
    QJsonObject timeline() const { return m_timeline; }
    QJsonObject liveTimeline() const;

    // "1:02.48".
    static QString timeText(qint64 ms);

    // Test support: the recording clock (ms into the track).
    void setRecordingClock(std::function<qint64()> clock) { m_clock = std::move(clock); }

signals:
    void changed();
    void ticked();

private:
    void follow(); // the operator selected another item
    void refresh();
    bool save();
    qint64 recordingPosition() const;
    void onSlideSent(int index);

    OperatorController &m_op;
    core::data::Database *m_db = nullptr;
    QString m_presentationId;
    QString m_title;
    QJsonObject m_timeline;
    QString m_trackPath;
    bool m_recording = false;
    std::unique_ptr<QMediaPlayer> m_player;
    std::unique_ptr<QAudioOutput> m_output;
    QElapsedTimer m_elapsed; // the clock when there is no track
    std::function<qint64()> m_clock;
    QTimer m_tick;
    QString m_message;
    RowModel m_model{{"time", "ms", "slide", "label", "text"}};
};

} // namespace lumen::app
