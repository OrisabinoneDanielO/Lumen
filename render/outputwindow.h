#pragma once

#include "renderclock.h"
#include "scenemodel.h"

#include <QImage>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QSize>
#include <QTimer>

#include <atomic>

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

    QImage grab();
    QQuickWindow *window() const { return m_window; }

private:
    void tick();

    RenderClock m_clock;
    SceneModel m_model;
    QQmlApplicationEngine m_engine;
    QQuickWindow *m_window = nullptr;
    std::atomic<quint64> m_frames{0};
    std::atomic<quint64> m_syncedSequence{0};
    std::atomic<quint64> m_presentedSequence{0};
    QTimer m_tick;
};

} // namespace lumen::render
