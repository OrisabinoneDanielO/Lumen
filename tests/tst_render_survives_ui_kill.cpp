#include "rendercheck.h"

#include "lumen/core/render/renderlink.h"
#include "lumen/core/show/scene.h"

#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <atomic>
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
// A renderer from an older Lumen: speaks protocol 9 only, and quits only
// when asked in version 9. Runs on its own thread (RenderLink waits for
// replies synchronously).
class OldRenderer : public QThread
{
public:
    explicit OldRenderer(QString name) : m_name(std::move(name)) {}
    std::atomic<bool> listening{false};
    std::atomic<bool> askedToQuitInItsVersion{false};

protected:
    void run() override
    {
        QLocalServer server;
        QLocalServer::removeServer(m_name);
        if (!server.listen(m_name))
            return;
        listening = true;
        QEventLoop loop;
        QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
            QLocalSocket *socket = server.nextPendingConnection();
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [&, socket] {
                while (socket->canReadLine()) {
                    const QJsonObject request = QJsonDocument::fromJson(socket->readLine()).object();
                    const int v = request.value("v").toInt();
                    if (v == 9 && request.value("cmd").toString() == QLatin1String("quit")) {
                        askedToQuitInItsVersion = true;
                        socket->write("{\"v\":9,\"ok\":true}\n");
                        socket->flush();
                        socket->disconnectFromServer();
                        server.close();
                        loop.quit();
                        return;
                    }
                    QJsonObject reply{{"v", 9}, {"ok", false},
                                      {"error", QStringLiteral("Unsupported protocol version %1 (renderer speaks 9)").arg(v)}};
                    if (request.contains("id"))
                        reply.insert("id", request.value("id"));
                    socket->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
                    socket->flush();
                }
            });
        });
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        loop.exec();
    }

private:
    QString m_name;
};

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
        // Never leave a renderer behind, even if the test failed (nor a stage
        // display the app may have started).
        RenderLink link(linkOptions());
        if (link.connectToRunning(500).ok())
            (void)link.quit();
        RenderLink::Options stageOptions = linkOptions();
        stageOptions.serverName += QStringLiteral("-stage");
        RenderLink stage(stageOptions);
        if (stage.connectToRunning(300).ok())
            (void)stage.quit();
    }

    // After an update, a renderer from the old version may still be running
    // (it outlives the app). It is asked to quit in its own protocol, and the
    // new renderer takes its place.
    void oldRendererIsReplaced()
    {
        OldRenderer old(m_server);
        old.start();
        QTRY_VERIFY(old.listening);
        RenderLink link(linkOptions());
        QVERIFY2(link.start().ok(), "the new renderer did not start");
        QVERIFY(old.askedToQuitInItsVersion);
        QVERIFY(old.wait(5000));
        auto status = link.request({{"cmd", "status"}});
        QVERIFY2(status.ok(), status.ok() ? "" : qPrintable(status.error().message));
        QCOMPARE(status.value().value("kind").toString(), QStringLiteral("audience"));
        (void)link.quit();
    }

    // ask(): the reply comes later, through the callback, without waiting
    // (how the operator window polls its outputs).
    void askDoesNotWait()
    {
        RenderLink link(linkOptions());
        QVERIFY(link.start().ok());
        QJsonObject got;
        QVERIFY(link.ask({{"cmd", "status"}}, [&got](const QJsonObject &reply) { got = reply; }).ok());
        QVERIFY(got.isEmpty()); // not yet: ask() returned at once
        QTRY_VERIFY(!got.isEmpty());
        QVERIFY(got.value("ok").toBool());
        QVERIFY(got.contains("videoErrors"));
        (void)link.quit();
    }

    // One Lumen at a time: a second start brings the running one forward and
    // quits, instead of sharing (and later closing) its audience output.
    void secondStartJoinsTheFirst()
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_dir.filePath(QStringLiteral("config")));
        env.insert(QStringLiteral("XDG_DATA_HOME"), m_dir.filePath(QStringLiteral("data")));
        const QStringList arguments{QStringLiteral("--no-output"), QStringLiteral("--render-server"), m_server,
                                    QStringLiteral("--quit-after=20000")};
        QProcess first;
        first.setProcessEnvironment(env);
        first.start(QStringLiteral(LUMEN_APP_PATH), arguments);
        QVERIFY(first.waitForStarted(10000));
        QTest::qWait(1500); // its window is up and listening

        QProcess second;
        second.setProcessEnvironment(env);
        second.start(QStringLiteral(LUMEN_APP_PATH), arguments);
        QVERIFY(second.waitForFinished(10000));
        QCOMPARE(second.exitCode(), 0);
        QVERIFY(second.readAllStandardOutput().contains("already running"));
        QCOMPARE(first.state(), QProcess::Running);

        first.terminate();
        if (!first.waitForFinished(5000))
            first.kill();
    }

    // Any version can ask a renderer to quit.
    void quitWorksFromAnyVersion()
    {
        RenderLink link(linkOptions());
        QVERIFY(link.start().ok());
        QLocalSocket raw;
        raw.connectToServer(m_server);
        QVERIFY(raw.waitForConnected(2000));
        raw.write("{\"v\":3,\"cmd\":\"quit\"}\n");
        raw.flush();
        QVERIFY(raw.waitForReadyRead(2000));
        QVERIFY(raw.readLine().contains("\"ok\":true"));
        QTRY_VERIFY_WITH_TIMEOUT(!RenderLink(linkOptions()).connectToRunning(100).ok(), 5000);
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
        // Its own settings and library (Linux): never the user's, which may
        // turn on a stage display or open their library.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_dir.filePath(QStringLiteral("config")));
        env.insert(QStringLiteral("XDG_DATA_HOME"), m_dir.filePath(QStringLiteral("data")));
        ui.setProcessEnvironment(env);
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
        restartedUi.setProcessEnvironment(ui.processEnvironment());
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
