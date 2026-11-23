#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QVideoFrame>

class QVideoSink;

namespace lumen::render {

// What the videos on screen have done (summed over every video shown).
struct VideoStats
{
    quint64 frames = 0;    // frames delivered for display
    quint64 dropped = 0;   // frames missing from the timeline (skipped)
    quint64 loops = 0;     // times a looping video started again
    quint64 stalls = 0;    // pauses in delivery long enough to see (> 100 ms)
    quint64 gpuFrames = 0; // frames that arrived as GPU textures (hardware path)
    double maxGapMs = 0;   // longest wall-clock gap between frames
    qint64 startMs = -1;   // last video: from being sent live to its first frame
    quint64 starts = 0;    // videos that showed their first frame
};

// Watches one video's frames. A frame's start time says where it sits in the
// video, so a jump of more than one frame duration means frames were
// skipped. A jump back to the start is a loop; it is clean if the last frame
// before it was the video's final frame.
class VideoMonitor : public QObject
{
    Q_OBJECT

public:
    // shownAt: the clock reading when the video was sent live.
    VideoMonitor(QVideoSink *sink, const QElapsedTimer &clock, qint64 shownAt, QObject *parent = nullptr);

    const VideoStats &stats() const { return m_stats; }
    bool attached() const { return !m_sink.isNull(); }

    // The video's length in microseconds, once known (for loop checks).
    void setDuration(qint64 microseconds) { m_durationUs = microseconds; }

private:
    void onFrame(const QVideoFrame &frame);

    QPointer<QVideoSink> m_sink;
    const QElapsedTimer &m_clock;
    qint64 m_shownAt;
    qint64 m_durationUs = 0;
    qint64 m_lastStartUs = -1;
    qint64 m_frameUs = 0;
    qint64 m_lastWallMs = -1;
    VideoStats m_stats;
};

} // namespace lumen::render
