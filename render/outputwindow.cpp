#include "outputwindow.h"

#include <QCursor>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Lumen_RenderPlugin)

namespace lumen::render {

OutputWindow::OutputWindow(QObject *parent) : QObject(parent)
{
    // Ask for a new frame every display refresh. With vsync the swap paces
    // this to the screen; without it (headless tests) the timer does.
    m_tick.setTimerType(Qt::PreciseTimer);
    m_tick.setInterval(16);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (m_window)
            m_window->update();
    });
}

OutputWindow::~OutputWindow() = default;

bool OutputWindow::create()
{
    m_engine.loadFromModule("Lumen.Render", "Output");
    if (m_engine.rootObjects().isEmpty())
        return false;
    m_window = qobject_cast<QQuickWindow *>(m_engine.rootObjects().constFirst());
    if (!m_window)
        return false;

    // frameSwapped comes from the render thread; only touch the atomic there.
    connect(m_window, &QQuickWindow::frameSwapped, this, [this] { ++m_frames; },
            Qt::DirectConnection);
    m_tick.start();
    return true;
}

void OutputWindow::place(QScreen *screen, bool fullscreen, QSize windowedSize)
{
    if (!m_window)
        return;
    if (screen)
        m_window->setScreen(screen);

    if (fullscreen) {
        m_window->setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        m_window->setCursor(Qt::BlankCursor);
        if (screen)
            m_window->setGeometry(screen->geometry());
        m_window->showFullScreen();
    } else {
        m_window->setFlags(Qt::Window);
        m_window->unsetCursor();
        m_window->resize(windowedSize);
        if (screen) {
            const QRect area = screen->availableGeometry();
            m_window->setPosition(area.center() - QPoint(windowedSize.width() / 2,
                                                         windowedSize.height() / 2));
        }
        m_window->show();
    }
}

void OutputWindow::setContent(const QColor &background, const QString &text)
{
    if (!m_window)
        return;
    m_window->setProperty("background", background);
    m_window->setProperty("text", text);
}

QImage OutputWindow::grab()
{
    return m_window ? m_window->grabWindow() : QImage();
}

} // namespace lumen::render
