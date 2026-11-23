#pragma once

#include "renderclock.h"
#include "scenemodel.h"
#include "videomonitor.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QSize>
#include <QTimer>

#include <atomic>
#include <memory>
#include <vector>

class QQuickWindow;
class QScreen;

namespace lumen::render {

// One output window and its scene. Frames are presented continuously at the
// display's refresh rate, and transitions run on this process's clock, so the
// picture stays live and fades finish whatever the UI does.
class OutputWindow : public QObject
{
    Q_OBJECT

public:
    explicit OutputWindow(QObject *parent = nullptr);
    ~OutputWindow() override;

    // Loads the scene. Returns false if it could not be created.
    bool create();

    // Fullscreen: borderless, always on top, no cursor, covering the screen.
    // Windowed: an ordinary window of windowedSize, centred on the screen.
    void place(QScreen *screen, bool fullscreen, QSize windowedSize = QSize(960, 540));

    // Shows a scene; each layer transitions as the scene says.
    void setScene(const core::show::Scene &scene);
    const core::show::Scene &scene() const { return m_model.scene(); }

    // Test support: freeze time and step it by hand.
    void setManualClock(bool manual);
    bool advanceClock(qint64 ms); // false if the clock is not manual

    // Frames presented so far (counted after each swap).
    quint64 framesPresented() const { return m_frames.load(); }

    // Sequence number of the scene shown in the most recently presented frame.
    quint64 presentedSequence() const { return m_presentedSequence.load(); }

    // Opens a video ahead of use so it starts at once when sent live.
    void preload(const QString &path) { m_model.preload(path); }
    // What the videos shown so far have done (batch 9).
    VideoStats videoStats() const;
    int videosPlaying() const;

    // How smoothly this process itself ran: the longest gap between ticks of
    // the main loop and between swapped frames, and how many exceeded 100 ms.
    // A video stall with a matching gap here means the whole process was held
    // up (by the system or the compositor), not the video decoder.
    struct Timing
    {
        double tickMaxGapMs = 0;
        quint64 tickStalls = 0;
        double swapMaxGapMs = 0;
        quint64 swapStalls = 0;
    };
    Timing timing() const;

    QImage grab();
    QQuickWindow *window() const { return m_window; }

private:
    void tick();
    void watchVideos();

    RenderClock m_clock;
    SceneModel m_model;
    QQmlApplicationEngine m_engine;
    QQuickWindow *m_window = nullptr;
    std::atomic<quint64> m_frames{0};
    std::atomic<quint64> m_syncedSequence{0};
    std::atomic<quint64> m_presentedSequence{0};
    QTimer m_tick;
    QElapsedTimer m_tickClock;
    qint64 m_lastTickMs = -1;
    Timing m_tickTiming;
    QElapsedTimer m_swapClock; // read on the render thread only
    qint64 m_lastSwapMs = -1;
    std::atomic<qint64> m_swapMaxGapUs{0};
    std::atomic<quint64> m_swapStalls{0};

    struct Watched
    {
        QPointer<QObject> output; // the VideoOutput item
        std::unique_ptr<VideoMonitor> monitor;
    };
    QElapsedTimer m_since;
    qint64 m_sceneAtMs = 0;
    std::vector<Watched> m_videos;
    VideoStats m_finished; // videos no longer on screen
    qint64 m_lastStartMs = -1;
};

} // namespace lumen::render
