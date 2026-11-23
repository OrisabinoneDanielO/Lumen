#include "outputwindow.h"

#include <QCursor>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QVideoSink>

#include <algorithm>
#include <QtQml/qqmlextensionplugin.h>

#include "lumen/core/show/textstyle.h"

Q_IMPORT_QML_PLUGIN(Lumen_RenderPlugin)
Q_IMPORT_QML_PLUGIN(Lumen_SlidesPlugin)

namespace lumen::render {

OutputWindow::OutputWindow(Kind kind, QObject *parent) : QObject(parent), m_kind(kind)
{
    m_since.start();
    m_tickClock.start();
    m_swapClock.start();
    // Advance transitions and ask for a new frame every display refresh. With
    // vsync the swap paces this to the screen; without it the timer does.
    m_tick.setTimerType(Qt::PreciseTimer);
    m_tick.setInterval(16);
    connect(&m_tick, &QTimer::timeout, this, &OutputWindow::tick);
}

OutputWindow::~OutputWindow()
{
    // The QML items go before the window that shows them.
    if (m_offscreen)
        m_engine.clearComponentCache();
}

void OutputWindow::setStreamFormat(QSize size, int fps)
{
    if (size.isValid())
        m_streamSize = size;
    if (fps > 0)
        m_streamFps = fps;
}

bool OutputWindow::createStream()
{
    // Drawn by Qt Quick's software renderer straight into a picture with an
    // alpha channel: the stream shows text and pictures (Slide and Props),
    // never video, so this is cheap, and the frames are where NDI and key and
    // fill need them, in memory.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    m_control = std::make_unique<QQuickRenderControl>();
    m_offscreen = std::make_unique<QQuickWindow>(m_control.get());
    m_offscreen->setColor(Qt::transparent);
    m_offscreen->resize(m_streamSize);
    m_offscreen->contentItem()->setSize(m_streamSize);

    m_engine.setInitialProperties({{QStringLiteral("scene"), QVariant::fromValue<QObject *>(&m_model)}});
    m_engine.loadFromModule("Lumen.Render", "StreamScene");
    auto *root = m_engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickItem *>(m_engine.rootObjects().constFirst());
    if (!root) {
        qWarning("lumen-render: the stream scene did not load");
        return false;
    }
    root->setParentItem(m_offscreen->contentItem());
    root->setSize(m_streamSize);

    m_frame = QImage(m_streamSize, QImage::Format_ARGB32_Premultiplied);
    m_frame.fill(Qt::transparent);
    m_offscreen->setRenderTarget(QQuickRenderTarget::fromPaintDevice(&m_frame));
    // The software renderer draws into the picture directly; initialize()
    // is only for the GPU (QRhi) renderers.
    const bool software = QQuickWindow::sceneGraphBackend() == QLatin1String("software");
    if (!software && !m_control->initialize()) {
        qWarning("lumen-render: the stream renderer could not start (%s)", qPrintable(QQuickWindow::sceneGraphBackend()));
        return false;
    }
    connect(m_control.get(), &QQuickRenderControl::renderRequested, this, [this] { m_dirty = true; });
    connect(m_control.get(), &QQuickRenderControl::sceneChanged, this, [this] { m_dirty = true; });
    m_window = m_offscreen.get();
    m_tick.setInterval(1000 / m_streamFps);
    m_tick.start();
    return true;
}

void OutputWindow::renderStream()
{
    if (!m_dirty)
        return;
    m_dirty = false;
    m_syncedSequence = m_model.scene().sequence;
    // No clearing here: the software renderer repaints only what changed
    // (clearing those parts to the window's transparent colour itself).
    const bool software = QQuickWindow::sceneGraphBackend() == QLatin1String("software");
    m_control->polishItems();
    if (!software)
        m_control->beginFrame();
    m_control->sync();
    m_control->render();
    if (!software)
        m_control->endFrame();
    m_presentedSequence = m_syncedSequence.load();
    ++m_frames;
    emit streamFrame(m_frame);
}

bool OutputWindow::create()
{
    core::show::registerOutputFonts();
    if (m_kind == Kind::Stream)
        return createStream();
    if (m_kind == Kind::Stage) {
        for (const char *file : {"Inter-Regular", "Inter-Medium", "Inter-SemiBold", "Inter-Bold"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/lumen/stage/fonts/%1.ttf").arg(QLatin1String(file)));
    }
    m_engine.setInitialProperties({{QStringLiteral("scene"), QVariant::fromValue<QObject *>(&m_model)}});
    m_engine.loadFromModule("Lumen.Render", m_kind == Kind::Stage ? "Stage" : "Output");
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
        const qint64 now = m_swapClock.nsecsElapsed() / 1000;
        if (m_lastSwapMs >= 0) {
            const qint64 gapUs = now - m_lastSwapMs;
            if (gapUs > m_swapMaxGapUs.load())
                m_swapMaxGapUs = gapUs;
            if (gapUs > 100000)
                ++m_swapStalls;
        }
        m_lastSwapMs = now;
    }, Qt::DirectConnection);
    m_tick.start();
    return true;
}

void OutputWindow::tick()
{
    const qint64 now = m_tickClock.elapsed();
    if (m_lastTickMs >= 0) {
        const double gap = double(now - m_lastTickMs);
        m_tickTiming.tickMaxGapMs = std::max(m_tickTiming.tickMaxGapMs, gap);
        if (gap > 100.0)
            ++m_tickTiming.tickStalls;
    }
    m_lastTickMs = now;
    m_model.update(m_clock.now());
    if (m_kind == Kind::Stream) {
        renderStream();
        return;
    }
    watchVideos();
    if (m_window)
        m_window->update();
}

namespace {

void collectVideoOutputs(QQuickItem *item, QList<QQuickItem *> &found)
{
    if (!item)
        return;
    if (item->objectName() == QLatin1String("videoOutput"))
        found << item;
    for (QQuickItem *child : item->childItems())
        collectVideoOutputs(child, found);
}

void add(VideoStats &into, const VideoStats &from)
{
    into.frames += from.frames;
    into.dropped += from.dropped;
    into.loops += from.loops;
    into.stalls += from.stalls;
    into.gpuFrames += from.gpuFrames;
    into.starts += from.starts;
    into.maxGapMs = std::max(into.maxGapMs, from.maxGapMs);
}

} // namespace

void OutputWindow::watchVideos()
{
    if (!m_window)
        return;
    // Videos that left the screen: keep their numbers.
    for (auto it = m_videos.begin(); it != m_videos.end();) {
        if (!it->output || !it->monitor->attached()) {
            add(m_finished, it->monitor->stats());
            if (it->monitor->stats().startMs >= 0)
                m_lastStartMs = it->monitor->stats().startMs;
            it = m_videos.erase(it);
        } else {
            it->monitor->setDuration(it->output->property("mediaDurationUs").toLongLong());
            ++it;
        }
    }
    // New videos.
    QList<QQuickItem *> outputs;
    collectVideoOutputs(m_window->contentItem(), outputs);
    for (QQuickItem *output : outputs) {
        const bool known = std::any_of(m_videos.begin(), m_videos.end(),
                                       [output](const Watched &w) { return w.output == output; });
        if (known)
            continue;
        auto *sink = output->property("videoSink").value<QVideoSink *>();
        if (!sink)
            continue;
        m_videos.push_back({output, std::make_unique<VideoMonitor>(sink, m_since, m_sceneAtMs)});
    }
}

VideoStats OutputWindow::videoStats() const
{
    VideoStats total = m_finished;
    for (const Watched &w : m_videos) {
        add(total, w.monitor->stats());
        if (w.monitor->stats().startMs >= 0)
            total.startMs = w.monitor->stats().startMs;
    }
    if (total.startMs < 0)
        total.startMs = m_lastStartMs;
    return total;
}

OutputWindow::Timing OutputWindow::timing() const
{
    Timing t = m_tickTiming;
    t.swapMaxGapMs = double(m_swapMaxGapUs.load()) / 1000.0;
    t.swapStalls = m_swapStalls.load();
    return t;
}

int OutputWindow::videosPlaying() const
{
    return int(std::count_if(m_videos.begin(), m_videos.end(), [](const Watched &w) { return w.output; }));
}

void OutputWindow::place(QScreen *screen, bool fullscreen, QSize windowedSize)
{
    if (!m_window || m_kind == Kind::Stream) // drawn off screen
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
    m_sceneAtMs = m_since.elapsed();
    m_model.apply(scene, m_clock.now());
    m_dirty = true;
    if (m_window && m_kind != Kind::Stream)
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
    m_dirty = true;
    if (m_window && m_kind != Kind::Stream)
        m_window->update();
    return true;
}

namespace {

void collectTexts(QQuickItem *item, const QString &prefix, QVariantMap &out)
{
    if (!item || !item->isVisible())
        return;
    if (item->objectName().startsWith(prefix))
        out.insert(item->objectName(), item->property("text"));
    for (QQuickItem *child : item->childItems())
        collectTexts(child, prefix, out);
}

} // namespace

QVariantMap OutputWindow::visibleTexts(const QString &prefix) const
{
    QVariantMap texts;
    if (m_window)
        collectTexts(m_window->contentItem(), prefix, texts);
    return texts;
}

QString OutputWindow::screenName() const
{
    return m_window && m_window->screen() ? m_window->screen()->name() : QString();
}

QImage OutputWindow::grab()
{
    if (m_kind == Kind::Stream) {
        renderStream(); // if anything changed since the last frame
        return m_frame.copy();
    }
    return m_window ? m_window->grabWindow() : QImage();
}

} // namespace lumen::render
