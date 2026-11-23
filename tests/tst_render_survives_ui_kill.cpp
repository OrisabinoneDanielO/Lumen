#include "rendercheck.h"

#include "lumen/core/render/renderlink.h"
#include "lumen/core/show/scene.h"

#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using lumen::core::render::RenderLink;
using namespace lumen::test;

// Batch 3 "Done when": killing the UI process leaves the last frame on screen.
//
// Starts the real lumen app with the experimental output on, waits for its
// test frame, hard-kills lumen, then checks lumen-render is still running,
// still presenting new frames, and still showing the same picture.
class TestRenderSurvivesUiKill : public QObject
{
    Q_OBJECT

private:
    QString m_server;
    QTemporaryDir m_dir;
    int m_grabs = 0;

    RenderLink::Options linkOptions() const
    {
        RenderLink::Options options;
        options.serverName = m_server;
        return options;
    }

    qint64 frames(RenderLink &link)
    {
        auto status = link.request({{"cmd", "status"}});
        return status.ok() ? status.value().value(QStringLiteral("frames")).toInteger() : -1;
    }

    QImage grab(RenderLink &link)
    {
        const QString path = m_dir.filePath(QStringLiteral("frame-%1.png").arg(++m_grabs));
        auto reply = link.request({{"cmd", "grab"}, {"path", path}}, 5000);
        return reply.ok() ? QImage(path) : QImage();
    }

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_server = QStringLiteral("lumen-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
    }

    void cleanup()
    {
        // Never leave a renderer behind, even if the test failed.
        RenderLink link(linkOptions());
        if (link.connectToRunning(500).ok())
            (void)link.quit();
    }

    void outputSurvivesUiKill()
    {
        const QColor testFrame(0x14, 0x21, 0x3D); // what lumen sends at start-up

        QProcess ui;
        ui.setProgram(QStringLiteral(LUMEN_APP_PATH));
        ui.setArguments({QStringLiteral("--experimental-output"),
                         QStringLiteral("--render-server"), m_server,
                         QStringLiteral("--render-executable"), QStringLiteral(LUMEN_RENDER_PATH),
                         QStringLiteral("--render-option=--test-mode"),
                         QStringLiteral("--render-option=--windowed")});
        ui.setProcessChannelMode(QProcess::ForwardedChannels);
        ui.start();
        QVERIFY2(ui.waitForStarted(10000), qPrintable(ui.errorString()));

        // The UI launches the renderer and sends its test frame.
        RenderLink probe(linkOptions());
        QTRY_VERIFY_WITH_TIMEOUT(probe.connectToRunning(100).ok(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(backgroundIs(grab(probe), testFrame), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(frames(probe) > 10, 10000);
        const QImage before = grab(probe);
        QVERIFY(textPixels(before) > 50);
        auto firstStatus = probe.request({{"cmd", "status"}});
        QVERIFY(firstStatus.ok());
        const qint64 renderPid = firstStatus.value().value(QStringLiteral("pid")).toInteger();
        QVERIFY(renderPid > 0);
        QVERIFY(renderPid != ui.processId());

        // Hard-kill the UI: SIGKILL on Linux and macOS, TerminateProcess on Windows.
        ui.kill();
        QVERIFY(ui.waitForFinished(10000));
        QCOMPARE(ui.exitStatus(), QProcess::CrashExit);

        // The renderer is still the same process...
        QTest::qWait(500);
        auto status = probe.request({{"cmd", "status"}});
        QVERIFY2(status.ok(), status.ok() ? "" : qPrintable(status.error().message));
        QCOMPARE(status.value().value(QStringLiteral("pid")).toInteger(), renderPid);

        // ...still presenting new frames (not frozen)...
        const qint64 afterKill = frames(probe);
        QTest::qWait(500);
        QVERIFY2(frames(probe) >= afterKill + 10, "the renderer stopped presenting frames");

        // ...and still showing the last frame the UI sent.
        const QImage after = grab(probe);
        QCOMPARE(after.size(), before.size());
        QVERIFY2(after == before, "the picture changed after the UI was killed");

        // A restarted UI reconnects to the same renderer and adopts what is
        // on air instead of blanking it or putting up its test frame (batch 4).
        QProcess restartedUi;
        restartedUi.setProgram(ui.program());
        restartedUi.setArguments(ui.arguments());
        restartedUi.setProcessChannelMode(QProcess::ForwardedChannels);
        restartedUi.start();
        QVERIFY(restartedUi.waitForStarted(10000));
        QTest::qWait(1500);
        QCOMPARE(restartedUi.state(), QProcess::Running);
        auto afterRestart = probe.request({{"cmd", "status"}});
        QVERIFY(afterRestart.ok());
        QCOMPARE(afterRestart.value().value(QStringLiteral("pid")).toInteger(), renderPid);
        QVERIFY2(grab(probe) == before, "a restarted UI changed the picture");
        restartedUi.kill();
        QVERIFY(restartedUi.waitForFinished(10000));

        // Another client can still drive the renderer (batch 4: scene messages).
        auto onAir = probe.request({{"cmd", "getScene"}});
        QVERIFY(onAir.ok());
        auto scene = lumen::core::show::sceneFromJson(onAir.value().value("scene").toObject());
        QVERIFY(scene);
        QVERIFY(scene->sequence > 0);
        scene->sequence += 1;
        scene->black = true;
        scene->overrideTransition = lumen::core::show::Transition::cut();
        QVERIFY(probe.request({{"cmd", "scene"}, {"scene", lumen::core::show::toJson(*scene)}}).ok());
        QTRY_VERIFY(backgroundIs(grab(probe), Qt::black));
        QCOMPARE(textPixels(grab(probe)), 0);

        // A normal quit ends the renderer.
        QVERIFY(probe.quit().ok());
        RenderLink gone(linkOptions());
        QTRY_VERIFY_WITH_TIMEOUT(!gone.connectToRunning(100).ok(), 5000);
    }
};

QTEST_GUILESS_MAIN(TestRenderSurvivesUiKill)
#include "tst_render_survives_ui_kill.moc"
