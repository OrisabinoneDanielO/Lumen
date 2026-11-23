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
    // Advance transitions and ask for a new frame every display refresh. With
    // vsync the swap paces this to the screen; without it the timer does.
    m_tick.setTimerType(Qt::PreciseTimer);
    m_tick.setInterval(16);
    connect(&m_tick, &QTimer::timeout, this, &OutputWindow::tick);
}

OutputWindow::~OutputWindow() = default;

bool OutputWindow::create()
{
    m_engine.setInitialProperties({{QStringLiteral("scene"), QVariant::fromValue<QObject *>(&m_model)}});
    m_engine.loadFromModule("Lumen.Render", "Output");
    if (m_engine.rootObjects().isEmpty())
        return false;
    m_window = qobject_cast<QQuickWindow *>(m_engine.rootObjects().constFirst());
    if (!m_window)
        return false;

    // Both signals come from the render thread. During synchronisation the GUI
    // thread is blocked, so reading the scene there is safe; the frame swapped
    // afterwards is the one that shows that scene.
    connect(m_window, &QQuickWindow::beforeSynchronizing, this,
            [this] { m_syncedSequence = m_model.scene().sequence; }, Qt::DirectConnection);
    connect(m_window, &QQuickWindow::frameSwapped, this, [this] {
        m_presentedSequence = m_syncedSequence.load();
        ++m_frames;
    }, Qt::DirectConnection);
    m_tick.start();
    return true;
}

void OutputWindow::tick()
{
    m_model.update(m_clock.now());
    if (m_window)
        m_window->update();
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

void OutputWindow::setScene(const core::show::Scene &scene)
{
    m_model.apply(scene, m_clock.now());
    if (m_window)
        m_window->update();
}

void OutputWindow::setManualClock(bool manual)
{
    m_clock.setManual(manual);
}

bool OutputWindow::advanceClock(qint64 ms)
{
    if (!m_clock.isManual())
        return false;
    m_clock.advance(ms);
    m_model.update(m_clock.now());
    if (m_window)
        m_window->update();
    return true;
}

QImage OutputWindow::grab()
{
    return m_window ? m_window->grabWindow() : QImage();
}

} // namespace lumen::render
