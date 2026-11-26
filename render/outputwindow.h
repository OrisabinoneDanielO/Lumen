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

class QQuickRenderControl;
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
    // Audience: the layered scene. Stage (batch 13): the stage display, drawn
    // from a stage state (current and next slide, clock, timer, message).
    // Stream (batch 15): only the Slide and Props layers, drawn off screen on
    // transparency into a picture each frame (streamFrame), for NDI and key
    // and fill; it has no window of its own.
    enum class Kind { Audience, Stage, Stream };
    explicit OutputWindow(Kind kind = Kind::Audience, QObject *parent = nullptr);
    Kind kind() const { return m_kind; }
    ~OutputWindow() override;

    // Loads the scene. Returns false if it could not be created.
    bool create();

    // Stream outputs: the picture size (before create(); 1920 x 1080 by
    // default) and frames per second.
    void setStreamFormat(QSize size, int fps);
    QSize streamSize() const { return m_streamSize; }
    // Where the stream's frames go (for status): "key and fill; NDI: ...".
    void setStreamStatus(const QString &status) { m_streamStatus = status; }
    QString streamStatus() const { return m_streamStatus; }

    // Fullscreen: borderless, always on top, no cursor, covering the screen.
    // Windowed: an ordinary window of windowedSize, centred on the screen.
    void place(QScreen *screen, bool fullscreen, QSize windowedSize = QSize(960, 540));

    // Shows a scene; each layer transitions as the scene says.
    void setScene(const core::show::Scene &scene);
    const core::show::Scene &scene() const { return m_model.scene(); }

    // Test support: freeze time and step it by hand.
    void setManualClock(bool manual);
    bool advanceClock(qint64 ms); // false if the clock is not manual
    // Test support: what the wall clock (for timers) reads now.
    void setWallClock(qint64 wallMs) { m_model.setWallClock(wallMs, m_clock.now()); }
    QJsonObject timelineStatus() const { return m_model.timelineStatus(m_clock.now()); }

    // Frames presented so far (counted after each swap).
    quint64 framesPresented() const { return m_frames.load(); }

    // Sequence number of the scene shown in the most recently presented frame.
    quint64 presentedSequence() const { return m_presentedSequence.load(); }

    // The stage display's state (stage windows only).
    void setStage(const QJsonObject &stage) { m_model.setStage(stage); }
    // The screen-setup label (empty object: none).
    void setIdentify(const QJsonObject &identify) { m_model.setIdentify(identify); }
    QJsonObject identify() const { return QJsonObject::fromVariantMap(m_model.identify()); }
    // Test support: the text of every visible item whose name starts with
    // `prefix`, by name.
    QVariantMap visibleTexts(const QString &prefix) const;
    QString screenName() const;

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

signals:
    // Stream outputs: a new frame (premultiplied ARGB, transparent where
    // nothing is drawn), sent when the picture changes.
    void streamFrame(const QImage &frame);

private:
    bool createStream();
    void renderStream();

    void tick();
    void watchVideos();

    Kind m_kind;
    std::unique_ptr<QQuickRenderControl> m_control;
    std::unique_ptr<QQuickWindow> m_offscreen;
    QImage m_frame;
    QSize m_streamSize{1920, 1080};
    int m_streamFps = 30;
    QString m_streamStatus;
    bool m_dirty = true;
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
