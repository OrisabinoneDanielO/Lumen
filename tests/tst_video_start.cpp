#include "lumen/core/render/renderlink.h"
#include "lumen/core/show/scene.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QThread>
#include <QUuid>

using namespace lumen::core::render;
using namespace lumen::core::show;

// Requirement (design doc): video starts in under 200 ms from the click when
// it was preloaded in Preview. The UI sends a slide live in under 50 ms
// (batch 5 test), so this measures the rest: from the renderer receiving the
// scene to the video's first frame, as the real lumen-render reports it.
class TestVideoStart : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<RenderLink> m_link;
    Scene m_scene;

    QJsonObject video()
    {
        auto reply = m_link->request({{"cmd", "status"}}, 5000);
        return reply.ok() ? reply.value().value("video").toObject() : QJsonObject();
    }

    void send(const Content &media)
    {
        ++m_scene.sequence;
        m_scene.layer(Layer::Media).content = media;
        QVERIFY(m_link->request({{"cmd", "scene"}, {"scene", toJson(m_scene)}}).ok());
    }

    // Shows a video and returns its start time once its first frame arrives.
    qint64 startOf(const QString &path)
    {
        const qint64 before = video().value("starts").toInteger();
        send(Content::video(path, false, 0.0));
        QElapsedTimer waiting;
        waiting.start();
        while (waiting.elapsed() < 10000) {
            const QJsonObject now = video();
            if (now.value("starts").toInteger() > before)
                return now.value("startMs").toInteger();
            QThread::msleep(10);
        }
        return -1;
    }

private slots:
    void initTestCase()
    {
        RenderLink::Options options;
        options.serverName = QStringLiteral("lumen-start-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        options.executable = QStringLiteral(LUMEN_RENDER_PATH);
        options.arguments = {QStringLiteral("--windowed"), QStringLiteral("--test-mode"),
                             QStringLiteral("--watch-pid=%1").arg(QCoreApplication::applicationPid())};
        m_link = std::make_unique<RenderLink>(options);
        auto started = m_link->start();
        QVERIFY2(started.ok(), started.ok() ? "" : qPrintable(started.error().message));
    }

    void cleanupTestCase()
    {
        if (m_link)
            (void)m_link->quit();
    }

    void preloadedVideoStartsQuickly_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("vp9 1080p") << QStringLiteral("loop-1080p-vp9.webm");
        QTest::newRow("h264 360p") << QStringLiteral("clip-360p-h264.mp4");
    }

    void preloadedVideoStartsQuickly()
    {
        QFETCH(QString, file);
        const QString path = QStringLiteral(LUMEN_MEDIA_DIR "/") + file;

        // Not preloaded (first use): reported, with a generous bound.
        const qint64 cold = startOf(path);
        QVERIFY2(cold >= 0, "the video never started");
        send(Content::empty());

        // In Preview: the UI asks the renderer to open it ahead of time.
        QVERIFY(m_link->request({{"cmd", "preload"}, {"video", path}}).ok());
        QThread::msleep(1500);
        const qint64 warm = startOf(path);
        send(Content::empty());
        qInfo("%s: cold start %lld ms, preloaded start %lld ms", qPrintable(file), cold, warm);
        QVERIFY2(warm >= 0, "the preloaded video never started");
        QVERIFY2(warm < 200, qPrintable(QStringLiteral("preloaded start took %1 ms").arg(warm)));
        QVERIFY2(cold < 2000, qPrintable(QStringLiteral("cold start took %1 ms").arg(cold)));
    }
};

QTEST_MAIN(TestVideoStart)
#include "tst_video_start.moc"
