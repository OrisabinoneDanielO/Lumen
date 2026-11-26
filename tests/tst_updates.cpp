#include "operatorharness.h"
#include "updatechecker.h"

#include <QNetworkAccessManager>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using lumen::app::UpdateChecker;

// Optional update checks, against a local stand-in for GitHub's "latest
// release" API: newer, same, older, no release yet, nonsense, no answer.
namespace {

class FakeReleases : public QObject
{
public:
    FakeReleases()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    m_request += socket->readAll();
                    if (!m_request.contains("\r\n\r\n"))
                        return;
                    ++requests;
                    lastRequest = m_request;
                    m_request.clear();
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/releases/latest").arg(m_server.serverPort())); }

    void release(const QString &tag)
    {
        status = "200 OK";
        body = QStringLiteral("{\"tag_name\": \"%1\", \"html_url\": "
                              "\"https://github.com/OrisabinoneDanielO/Lumen/releases/tag/%1\", \"draft\": false}")
                   .arg(tag)
                   .toUtf8();
    }

    QByteArray status = "200 OK";
    QByteArray body;
    int requests = 0;
    QByteArray lastRequest;

private:
    QTcpServer m_server;
    QByteArray m_request;
};

UpdateChecker::Options optionsFor(const QUrl &url, const QString &current = QStringLiteral("0.1.0"))
{
    UpdateChecker::Options options;
    options.url = url;
    options.currentVersion = current;
    options.timeoutMs = 3000;
    return options;
}

} // namespace

class TestUpdates : public QObject
{
    Q_OBJECT

private slots:
    void init() { QSettings().remove(QStringLiteral("updates")); }

    void comparesVersions()
    {
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("0.2.0"), QStringLiteral("0.1.0")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("v0.1.0"), QStringLiteral("0.1.0")), 0);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("0.1"), QStringLiteral("0.1.0")), 0);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("0.9.9"), QStringLiteral("0.10.0")), -1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0.0-beta.2"), QStringLiteral("1.0.0")), 0);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("2"), QStringLiteral("1.9.9")), 1);
    }

    // Off by default, and no network object at all until a check.
    void offUntilAsked()
    {
        FakeReleases github;
        UpdateChecker updates(optionsFor(github.url()));
        QVERIFY(!updates.automatic());
        QVERIFY(updates.findChildren<QNetworkAccessManager *>().isEmpty());
        QTest::qWait(100);
        QCOMPARE(github.requests, 0);
    }

    void newerReleaseIsOffered()
    {
        FakeReleases github;
        github.release(QStringLiteral("v0.2.0"));
        UpdateChecker updates(optionsFor(github.url()));
        QSignalSpy found(&updates, &UpdateChecker::updateFound);
        updates.check();
        QVERIFY(updates.checking());
        QTRY_VERIFY(!updates.checking());
        QVERIFY(updates.available());
        QCOMPARE(updates.latestVersion(), QStringLiteral("0.2.0"));
        QCOMPARE(found.count(), 1);
        QVERIFY(updates.status().contains(QStringLiteral("0.2.0")));
        QCOMPARE(updates.downloadUrl().toString(),
                 QStringLiteral("https://github.com/OrisabinoneDanielO/Lumen/releases/tag/v0.2.0"));
        // The request names the program and nothing else about the user.
        // (Header names are case-insensitive; Qt 6.8 sends them in lower case.)
        const QByteArray request = github.lastRequest.toLower();
        QVERIFY2(request.contains("user-agent: lumenpresenter/0.1.0"), request.constData());
        QVERIFY(!request.contains("cookie"));
    }

    void sameOrOlderIsNotOffered()
    {
        FakeReleases github;
        for (const QString &tag : {QStringLiteral("v0.1.0"), QStringLiteral("v0.0.9")}) {
            github.release(tag);
            UpdateChecker updates(optionsFor(github.url()));
            updates.check();
            QTRY_VERIFY(!updates.checking());
            QVERIFY2(!updates.available(), qPrintable(tag));
            QCOMPARE(updates.status(), QStringLiteral("Lumen 0.1.0 is the newest version."));
        }
    }

    void noReleaseYet()
    {
        FakeReleases github;
        github.status = "404 Not Found";
        github.body = "{\"message\": \"Not Found\"}";
        UpdateChecker updates(optionsFor(github.url()));
        updates.check();
        QTRY_VERIFY(!updates.checking());
        QVERIFY(!updates.available());
        QCOMPARE(updates.status(), QStringLiteral("Lumen 0.1.0 is the newest version."));
    }

    void nonsenseIsReported()
    {
        FakeReleases github;
        github.body = "<html>rate limited</html>";
        UpdateChecker updates(optionsFor(github.url()));
        updates.check();
        QTRY_VERIFY(!updates.checking());
        QVERIFY(!updates.available());
        QVERIFY(updates.status().startsWith(QStringLiteral("Could not check for updates")));
    }

    void noAnswerIsReported()
    {
        QTcpServer closed;
        QVERIFY(closed.listen(QHostAddress::LocalHost));
        const quint16 port = closed.serverPort();
        closed.close(); // nothing listens there now
        UpdateChecker updates(optionsFor(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(port))));
        updates.check();
        QTRY_VERIFY_WITH_TIMEOUT(!updates.checking(), 5000);
        QVERIFY(!updates.available());
        QVERIFY(updates.status().startsWith(QStringLiteral("Could not check for updates")));
    }

    void checkAtStartIsRemembered()
    {
        {
            UpdateChecker updates;
            updates.setAutomatic(true);
        }
        UpdateChecker next;
        QVERIFY(next.automatic());
        next.setAutomatic(false);
        QVERIFY(!UpdateChecker().automatic());
    }

    // Plan ▾ → Check for updates… opens the panel; Check now finds the
    // newer release; the menu then names it.
    void fromThePlanMenu()
    {
        FakeReleases github;
        github.release(QStringLiteral("v0.3.1"));
        lumen::test::OperatorHarness ui;
        ui.updateOptions = optionsFor(github.url());
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));

        QVERIFY(ui.click("planMenuButton"));
        QTRY_VERIFY(ui.item("planUpdates"));
        QCOMPARE(ui.item("planUpdates")->property("text").toString(), QStringLiteral("Check for updates…"));
        QVERIFY(ui.click("planUpdates"));
        QTRY_VERIFY(ui.item("updatePopover")->property("active").toBool());
        QTRY_VERIFY(ui.item("updateCheck"));
        QVERIFY(!ui.item("updateDownload")->isVisible());
        QCOMPARE(github.requests, 0);

        QVERIFY(ui.click("updateCheck"));
        QTRY_VERIFY(ui.updates->available());
        QTRY_VERIFY(ui.item("updateDownload")->isVisible());
        QCOMPARE(ui.item("updateDownload")->property("text").toString(), QStringLiteral("Download 0.3.1"));
        QVERIFY(ui.item("updateStatus")->property("text").toString().contains(QStringLiteral("0.3.1")));

        QVERIFY(ui.click("updateAutomatic"));
        QVERIFY(ui.updates->automatic());
        QVERIFY(ui.click("updateAutomatic"));
        QVERIFY(!ui.updates->automatic());

        QVERIFY(ui.click("planMenuButton"));
        QTRY_COMPARE(ui.item("planUpdates")->property("text").toString(), QStringLiteral("Lumen 0.3.1 is available…"));
    }
};

int main(int argc, char *argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Lumen Tests"));
    QGuiApplication::setApplicationName(QStringLiteral("tst_updates"));
    TestUpdates test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_updates.moc"
