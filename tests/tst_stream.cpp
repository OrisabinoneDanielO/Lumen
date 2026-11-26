#include "keyfill.h"
#include "ndisink.h"
#include "outputwindow.h"
#include "streamcontroller.h"
#include "rendercheck.h"

#include "lumen/core/render/renderlink.h"
#include "lumen/core/show/outputbridge.h"
#include "lumen/core/show/showcontroller.h"

#include <QDeadlineTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUuid>

using namespace lumen;
using namespace lumen::core::show;
using render::OutputWindow;

// Batch 15 "Done when": the stream output shows only Slide and Props, on
// transparency. Also the key and fill pictures made from it, and the NDI
// plugin's behaviour when the NDI Runtime is not installed.
namespace {

const QColor navy{0x14, 0x21, 0x3D};

Scene fullScene()
{
    Scene scene;
    scene.sequence = 1;
    scene.layer(Layer::Background).content = Content::fill(navy);
    scene.layer(Layer::Media).content = Content::image(QStringLiteral(LUMEN_MEDIA_DIR "/bars-720p.png"));
    scene.layer(Layer::Slide).content = Content::textContent(QStringLiteral("Amazing grace! How sweet the sound"));
    scene.layer(Layer::Props).content = Content::propContent(
        QJsonObject{{"lowerThird", QJsonObject{{"id", "a"}, {"line1", "Pastor Ade Johnson"}, {"line2", "Lead pastor"}}}});
    scene.layer(Layer::Messages).content = Content::textContent(QStringLiteral("Parent of 42"));
    return scene;
}

int alphaAt(const QImage &frame, double x, double y)
{
    return qAlpha(frame.pixel(int(x * (frame.width() - 1)), int(y * (frame.height() - 1))));
}

// Pixels in a band (fractions of the height) that are at least partly
// opaque.
int opaqueBetween(const QImage &frame, double top, double bottom)
{
    int count = 0;
    for (int y = int(top * frame.height()); y < int(bottom * frame.height()); y += 2) {
        for (int x = 0; x < frame.width(); x += 2)
            count += qAlpha(frame.pixel(x, y)) > 128 ? 1 : 0;
    }
    return count;
}

} // namespace

class TestStream : public QObject
{
    Q_OBJECT

private slots:
    void showsOnlySlideAndPropsOnTransparency()
    {
        OutputWindow stream(OutputWindow::Kind::Stream);
        stream.setStreamFormat(QSize(960, 540), 30);
        QVERIFY(stream.create());
        stream.setManualClock(true);
        stream.setScene(fullScene());
        const QImage frame = stream.grab();
        QCOMPARE(frame.size(), QSize(960, 540));
        QVERIFY(frame.hasAlphaChannel());

        // Background (navy) and Media (a picture filling the frame) are left
        // out: the corners are fully transparent.
        for (const auto &[x, y] : {std::pair{0.01, 0.3}, std::pair{0.99, 0.5}, std::pair{0.5, 0.65}, std::pair{0.2, 0.35}})
            QCOMPARE(alphaAt(frame, x, y), 0);
        // Messages (the top banner) are left out too.
        QCOMPARE(opaqueBetween(frame, 0.0, 0.12), 0);
        // Slide text in the middle and the lower third are there, opaque.
        QVERIFY(opaqueBetween(frame, 0.3, 0.7) > 100);
        QVERIFY(opaqueBetween(frame, 0.74, 0.9) > 500);
        QCOMPARE(stream.presentedSequence(), quint64(1));

        // Black takes the stream to full transparency.
        Scene black = fullScene();
        black.sequence = 2;
        black.black = true;
        black.overrideTransition = Transition::cut();
        stream.setScene(black);
        QCOMPARE(opaqueBetween(stream.grab(), 0.0, 1.0), 0);
    }

    void framesFollowChanges()
    {
        OutputWindow stream(OutputWindow::Kind::Stream);
        stream.setStreamFormat(QSize(640, 360), 30);
        QVERIFY(stream.create());
        QSignalSpy frames(&stream, &OutputWindow::streamFrame);
        stream.setScene(fullScene());
        QVERIFY(frames.wait(2000));
        const QImage first = frames.last().first().value<QImage>();
        QVERIFY(opaqueBetween(first, 0.3, 0.7) > 50);
        // Once it has settled, nothing changes: no new frames (the picture
        // is only redrawn when it changes).
        QTest::qWait(500);
        frames.clear();
        QTest::qWait(500);
        QCOMPARE(frames.size(), 0);
        Scene cleared = fullScene();
        cleared.sequence = 2;
        cleared.layer(Layer::Slide).content = Content::empty();
        cleared.layer(Layer::Props).content = Content::empty();
        stream.setScene(cleared);
        QVERIFY(frames.wait(2000));
        QTRY_COMPARE(opaqueBetween(frames.last().first().value<QImage>(), 0.0, 1.0), 0);
    }

    // Key and fill: the fill is the picture over black, the key its
    // transparency as grey (white where fully opaque).
    void keyAndFill()
    {
        QImage frame(4, 1, QImage::Format_ARGB32_Premultiplied);
        frame.setPixel(0, 0, qPremultiply(qRgba(255, 0, 0, 255))); // opaque red
        frame.setPixel(1, 0, qPremultiply(qRgba(0, 0, 255, 128))); // half blue
        frame.setPixel(2, 0, qRgba(0, 0, 0, 0));                    // nothing
        frame.setPixel(3, 0, qPremultiply(qRgba(255, 255, 255, 64)));
        const QImage fill = render::fillOf(frame);
        const QImage key = render::keyOf(frame);
        QCOMPARE(QColor(fill.pixel(0, 0)), QColor(255, 0, 0));
        QVERIFY(test::near(QColor(fill.pixel(1, 0)), QColor(0, 0, 128), 2));
        QCOMPARE(QColor(fill.pixel(2, 0)), QColor(0, 0, 0));
        QCOMPARE(qGray(key.pixel(0, 0)), 255);
        QVERIFY(std::abs(qGray(key.pixel(1, 0)) - 128) <= 1);
        QCOMPARE(qGray(key.pixel(2, 0)), 0);
        QVERIFY(std::abs(qGray(key.pixel(3, 0)) - 64) <= 1);
    }

    void ndiWithoutTheRuntime()
    {
        // With no NDI Runtime (none here), the plugin says so and does
        // nothing else.
        qputenv("LUMEN_NDI_LIBRARY", "/no/such/libndi.so");
        render::NdiSink ndi(QStringLiteral("Lumen test"));
        QVERIFY(!ndi.isSending());
        QVERIFY2(ndi.status().contains(QStringLiteral("NDI Runtime")), qPrintable(ndi.status()));
        QImage frame(16, 9, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::red);
        ndi.frame(frame); // harmless
        qunsetenv("LUMEN_NDI_LIBRARY");
    }

    // The real stream renderer, as the app starts it.
    void streamRendererProcess()
    {
        core::render::RenderLink::Options options;
        options.serverName = QStringLiteral("lumen-stream-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        options.executable = QStringLiteral(LUMEN_RENDER_PATH);
        options.arguments = {QStringLiteral("--stream"), QStringLiteral("--size=960x540"), QStringLiteral("--test-mode"),
                             QStringLiteral("--watch-pid=%1").arg(QCoreApplication::applicationPid())};
        core::render::RenderLink link(options);
        auto started = link.start();
        QVERIFY2(started.ok(), started.ok() ? "" : qPrintable(started.error().message));
        auto status = link.request({{"cmd", "status"}});
        QVERIFY(status.ok());
        QCOMPARE(status.value().value("kind").toString(), QStringLiteral("stream"));
        QVERIFY(link.request({{"cmd", "scene"}, {"scene", toJson(fullScene())}}).ok());

        QTest::qWait(200); // a few frames: the grab must not depend on catching the first
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("stream.png"));
        QVERIFY(link.request({{"cmd", "grab"}, {"path", path}}).ok());
        const QImage frame(path);
        if (const QString dump = qEnvironmentVariable("LUMEN_STREAM_DUMP"); !dump.isEmpty())
            frame.save(dump);
        QCOMPARE(frame.size(), QSize(960, 540));
        QCOMPARE(alphaAt(frame, 0.01, 0.3), 0);
        QVERIFY(opaqueBetween(frame, 0.74, 0.9) > 500);

        // The app's bridge follows the show without taking the stream's
        // scene as its own (the main output is the one that survives a UI
        // crash): the show's empty scene is sent, not adopted.
        ShowController show;
        core::show::OutputBridge bridge(show, link);
        bridge.follow();
        show.setContent(Layer::Slide, Content::textContent(QStringLiteral("Be thou my vision")));
        QTRY_COMPARE(sceneFromJson(link.request({{"cmd", "getScene"}}).value().value("scene").toObject())
                         ->layer(Layer::Slide).content.text,
                     QStringLiteral("Be thou my vision"));
        QVERIFY(show.scene().layer(Layer::Props).content.isEmpty());
        (void)link.quit();
    }

    void streamSettings()
    {
        app::StreamController off(false);
        QCOMPARE(off.renderArguments(), (QStringList{"--stream", "--key-fill"}));
        off.setNdi(true); // not available: ignored
        QVERIFY(!off.ndi());
        QCOMPARE(off.renderArguments(), (QStringList{"--stream", "--key-fill"}));

        app::StreamController ndi(true);
        QSignalSpy restarts(&ndi, &app::StreamController::restartRequested);
        ndi.setKeyFill(false);
        ndi.setNdi(true);
        ndi.setNdiName(QStringLiteral("Main hall"));
        QCOMPARE(ndi.renderArguments(), (QStringList{"--stream", "--ndi=Main hall"}));
        QCOMPARE(restarts.size(), 0); // not running
        ndi.setConnected(true, QStringLiteral("NDI: sending"));
        ndi.setKeyFill(true);
        QCOMPARE(restarts.size(), 1); // running: restarted with the new choice
    }
};

QTEST_MAIN(TestStream)
#include "tst_stream.moc"
