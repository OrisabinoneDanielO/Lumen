#include "videomonitor.h"

#include <QVideoSink>

#include <cmath>

namespace lumen::render {

VideoMonitor::VideoMonitor(QVideoSink *sink, const QElapsedTimer &clock, qint64 shownAt, QObject *parent)
    : QObject(parent), m_sink(sink), m_clock(clock), m_shownAt(shownAt)
{
    connect(sink, &QVideoSink::videoFrameChanged, this, &VideoMonitor::onFrame);
}

void VideoMonitor::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    const qint64 wall = m_clock.elapsed();
    const qint64 start = frame.startTime();
    if (frame.endTime() > start && start >= 0)
        m_frameUs = frame.endTime() - start;

    ++m_stats.frames;
    if (frame.handleType() == QVideoFrame::RhiTextureHandle)
        ++m_stats.gpuFrames;
    if (m_stats.frames == 1) {
        m_stats.startMs = wall - m_shownAt;
        m_stats.starts = 1;
    }

    if (m_lastWallMs >= 0) {
        const double gap = double(wall - m_lastWallMs);
        m_stats.maxGapMs = std::max(m_stats.maxGapMs, gap);
        if (gap > 100.0)
            ++m_stats.stalls;
    }
    m_lastWallMs = wall;

    if (m_lastStartUs >= 0 && m_frameUs > 0 && start >= 0) {
        if (start < m_lastStartUs) {
            // Back to the start: a loop. Frames missing at the end or the
            // beginning count as dropped.
            ++m_stats.loops;
            if (m_durationUs > 0) {
                const qint64 missingAtEnd = (m_durationUs - (m_lastStartUs + m_frameUs)) / m_frameUs;
                if (missingAtEnd > 0)
                    m_stats.dropped += quint64(missingAtEnd);
            }
            const qint64 missingAtStart = start / m_frameUs;
            if (missingAtStart > 0)
                m_stats.dropped += quint64(missingAtStart);
        } else {
            const double steps = double(start - m_lastStartUs) / double(m_frameUs);
            if (steps > 1.5)
                m_stats.dropped += quint64(std::llround(steps) - 1);
        }
    }
    m_lastStartUs = start;
}

} // namespace lumen::render
