#include "outputwindow.h"
#include "rendercheck.h"
#include "renderserver.h"

#include "lumen/core/render/protocol.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QLocalSocket>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace lumen::core::render;
using namespace lumen::core::show;
using namespace lumen::render;
using namespace lumen::test;

namespace {

QJsonObject request(const char *command, QJsonObject fields = {})
{
    fields.insert(QStringLiteral("v"), protocolVersion);
    fields.insert(QStringLiteral("cmd"), QLatin1String(command));
    return fields;
}

bool ok(const QJsonObject &reply)
{
    return reply.value(QStringLiteral("ok")).toBool();
}

Scene colourAndText(const QColor &background, const QString &text, quint64 sequence)
{
    Scene scene;
    scene.sequence = sequence;
    scene.layer(Layer::Background).content = Content::fill(background);
    if (!text.isEmpty())
        scene.layer(Layer::Slide).content = Content::textContent(text);
    return scene;
}

// The batch 4 replacement for batch 3's "show" command.
QJsonObject sceneRequest(const QColor &background, const QString &text, quint64 sequence)
{
    return request("scene", {{"scene", toJson(colourAndText(background, text, sequence))}});
}

QString uniqueName()
{
    return QStringLiteral("lumen-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

} // namespace

// The renderer's command handling and its local socket.
class TestRenderServer : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<OutputWindow> m_output;

private slots:
    void init()
    {
        m_output = std::make_unique<OutputWindow>();
        QVERIFY(m_output->create());
        m_output->place(QGuiApplication::primaryScreen(), false, QSize(320, 180));
        QVERIFY(QTest::qWaitForWindowExposed(m_output->window()));
    }

    void cleanup() { m_output.reset(); }

    void sceneChangesTheFrame()
    {
        RenderServer server(*m_output, false);
        QVERIFY(ok(server.handle(sceneRequest(QColor(0x14, 0x21, 0x3D), "Hello", 1))));
        QTRY_VERIFY(backgroundIs(m_output->grab(), QColor(0x14, 0x21, 0x3D)));
    }

    void sceneRejectsMalformed()
    {
        RenderServer server(*m_output, false);
        QVERIFY(!ok(server.handle(request("scene"))));
        QJsonObject badColour = toJson(colourAndText(Qt::red, "x", 1));
        QJsonArray layers = badColour.value("layers").toArray();
        layers[0] = QJsonObject{{"content", QJsonObject{{"kind", "colour"}, {"colour", "not a colour"}}}};
        badColour.insert("layers", layers);
        QVERIFY(!ok(server.handle(request("scene", {{"scene", badColour}}))));
        QJsonObject fewLayers = toJson(colourAndText(Qt::red, "x", 1));
        fewLayers.insert("layers", QJsonArray{});
        QVERIFY(!ok(server.handle(request("scene", {{"scene", fewLayers}}))));
    }

    // A snapshot older than what is on air must not win (batch 4).
    void staleSceneIsRefused()
    {
        RenderServer server(*m_output, false);
        QVERIFY(ok(server.handle(sceneRequest(Qt::blue, "", 5))));
        const QJsonObject stale = server.handle(sceneRequest(Qt::red, "", 4));
        QVERIFY(!ok(stale));
        QVERIFY(stale.value("error").toString().contains(QLatin1String("Stale")));
        QVERIFY(ok(server.handle(sceneRequest(Qt::blue, "", 5)))); // re-sending is fine
        QTRY_VERIFY(backgroundIs(m_output->grab(), Qt::blue));
    }

    void getSceneReturnsWhatIsOnAir()
    {
        RenderServer server(*m_output, false);
        const Scene sent = colourAndText(QColor(0x20, 0x30, 0x40), "On air", 7);
        QVERIFY(ok(server.handle(request("scene", {{"scene", toJson(sent)}}))));
        const QJsonObject reply = server.handle(request("getScene"));
        QVERIFY(ok(reply));
        const auto onAir = sceneFromJson(reply.value("scene").toObject());
        QVERIFY(onAir);
        QVERIFY(*onAir == sent);
    }

    void clockNeedsTestMode()
    {
        RenderServer normal(*m_output, false);
        QVERIFY(!ok(normal.handle(request("clock", {{"mode", "manual"}}))));

        RenderServer testing(*m_output, true);
        QVERIFY(!ok(testing.handle(request("clock", {{"advanceMs", 10}})))); // not manual yet
        QVERIFY(ok(testing.handle(request("clock", {{"mode", "manual"}}))));
        QVERIFY(ok(testing.handle(request("clock", {{"advanceMs", 10}}))));
        QVERIFY(ok(testing.handle(request("clock", {{"mode", "real"}}))));
        QVERIFY(!ok(testing.handle(request("clock"))));
    }

    void statusReportsFramesAndPid()
    {
        RenderServer server(*m_output, false);
        QTRY_VERIFY(m_output->framesPresented() > 0);
        const QJsonObject reply = server.handle(request("status"));
        QVERIFY(ok(reply));
        QVERIFY(reply.value(QStringLiteral("frames")).toInteger() > 0);
        QCOMPARE(reply.value(QStringLiteral("pid")).toInteger(), QCoreApplication::applicationPid());
    }

    // Writing files on request is for tests only.
    void grabNeedsTestMode()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("frame.png"));

        RenderServer normal(*m_output, false);
        QVERIFY(!ok(normal.handle(request("grab", {{"path", path}}))));
        QVERIFY(!QFile::exists(path));

        RenderServer testing(*m_output, true);
        QVERIFY(ok(testing.handle(request("grab", {{"path", path}}))));
        QVERIFY(!QImage(path).isNull());
        QVERIFY(!ok(testing.handle(request("grab"))));
    }

    void rejectsUnknownCommandAndVersion()
    {
        RenderServer server(*m_output, false);
        QVERIFY(!ok(server.handle(request("explode"))));
        QJsonObject future = request("status");
        future.insert(QStringLiteral("v"), protocolVersion + 1);
        QVERIFY(!ok(server.handle(future)));
        QVERIFY(!ok(server.handle({{"cmd", "status"}})));
    }

    void answersOverTheSocket()
    {
        RenderServer server(*m_output, false);
        const QString name = uniqueName();
        QString error;
        QVERIFY2(server.listen(name, &error), qPrintable(error));

        QLocalSocket client;
        client.connectToServer(name);
        QVERIFY(client.waitForConnected(2000));

        // Two requests in one write, plus a line that is not JSON.
        QJsonObject scene = sceneRequest(QColor(0x20, 0x30, 0x40), "", 1);
        scene.insert("id", "first");
        client.write(encode(scene) + encode({{"cmd", "status"}}) + "garbage\n");
        QByteArray received;
        QTRY_VERIFY((received += client.readAll()).count('\n') >= 3);
        const QList<QByteArray> lines = received.trimmed().split('\n');
        QCOMPARE(lines.size(), 3);
        QList<QJsonObject> replies;
        for (const QByteArray &line : lines) {
            const auto reply = decode(line);
            QVERIFY2(reply, line.constData());
            replies << *reply;
        }
        QVERIFY(ok(replies[0]));
        QCOMPARE(replies[0].value("id").toString(), QStringLiteral("first")); // ids are echoed
        QVERIFY(!replies[1].contains("id"));
        QVERIFY2(replies[1].value(QStringLiteral("frames")).isDouble(), lines[1].constData());
        QVERIFY(!ok(replies[2]));
        QTRY_VERIFY(backgroundIs(m_output->grab(), QColor(0x20, 0x30, 0x40)));
    }

    // The heart of batch 3: a client vanishing changes nothing on screen.
    void clientDisconnectKeepsTheFrame()
    {
        RenderServer server(*m_output, true);
        const QString name = uniqueName();
        QVERIFY(server.listen(name, nullptr));
        QSignalSpy quitSpy(&server, &RenderServer::quitRequested);

        {
            QLocalSocket client;
            client.connectToServer(name);
            QVERIFY(client.waitForConnected(2000));
            client.write(encode(sceneRequest(QColor(0x14, 0x21, 0x3D), "Still here", 1)));
            QTRY_VERIFY(client.canReadLine());
            client.abort(); // like a crash: no "quit"
        }
        QTRY_VERIFY(backgroundIs(m_output->grab(), QColor(0x14, 0x21, 0x3D)));
        const quint64 frames = m_output->framesPresented();
        QTest::qWait(300);
        QVERIFY(m_output->framesPresented() > frames);
        QVERIFY(textPixels(m_output->grab()) > 50);
        QCOMPARE(quitSpy.count(), 0);
    }

    void quitIsRequestedAfterReplying()
    {
        RenderServer server(*m_output, false);
        const QString name = uniqueName();
        QVERIFY(server.listen(name, nullptr));
        QSignalSpy quitSpy(&server, &RenderServer::quitRequested);

        QLocalSocket client;
        client.connectToServer(name);
        QVERIFY(client.waitForConnected(2000));
        client.write(encode({{"cmd", "quit"}}));
        QTRY_VERIFY(client.canReadLine());
        const auto reply = decode(client.readLine());
        QVERIFY(reply);
        QVERIFY(ok(*reply));
        QTRY_COMPARE(quitSpy.count(), 1);
    }

    void secondRendererWithSameNameIsRefused()
    {
        RenderServer first(*m_output, false);
        const QString name = uniqueName();
        QVERIFY(first.listen(name, nullptr));
        RenderServer second(*m_output, false);
        QString error;
        QVERIFY(!second.listen(name, &error));
        QVERIFY(error.contains(QLatin1String("already running")));
    }
};

QTEST_MAIN(TestRenderServer)
#include "tst_render_server.moc"
