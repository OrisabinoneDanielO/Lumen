#include "outputwindow.h"
#include "rendercheck.h"
#include "renderserver.h"

#include "lumen/core/render/protocol.h"

#include <QGuiApplication>
#include <QLocalSocket>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace lumen::core::render;
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

    void showChangesTheFrame()
    {
        RenderServer server(*m_output, false);
        QVERIFY(ok(server.handle(request("show", {{"colour", "#14213D"}, {"text", "Hello"}}))));
        QTRY_VERIFY(backgroundIs(m_output->grab(), QColor(0x14, 0x21, 0x3D)));
    }

    void showRejectsBadColour()
    {
        RenderServer server(*m_output, false);
        const QJsonObject reply = server.handle(request("show", {{"colour", "not a colour"}}));
        QVERIFY(!ok(reply));
        QVERIFY(reply.value(QStringLiteral("error")).toString().contains(QLatin1String("colour")));
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
        client.write(encode({{"cmd", "show"}, {"colour", "#203040"}, {"text", ""}})
                     + encode({{"cmd", "status"}}) + "garbage\n");
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
            client.write(encode({{"cmd", "show"}, {"colour", "#14213D"}, {"text", "Still here"}}));
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
