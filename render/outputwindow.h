#pragma once

#include <QColor>
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
// display's refresh rate, so the picture stays live whatever the UI does.
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

    void setContent(const QColor &background, const QString &text);

    // Frames presented so far (counted after each swap).
    quint64 framesPresented() const { return m_frames.load(); }

    QImage grab();
    QQuickWindow *window() const { return m_window; }

private:
    QQmlApplicationEngine m_engine;
    QQuickWindow *m_window = nullptr;
    std::atomic<quint64> m_frames{0};
    QTimer m_tick;
};

} // namespace lumen::render
