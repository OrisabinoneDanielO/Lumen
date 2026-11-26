#include "planstore.h"
#include "sampleservice.h"
#include "servicedata.h"
#include "sharing.h"

#include "lumen/core/data/repository.h"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUdpSocket>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#endif

using namespace lumen;
using namespace lumen::app;
using namespace lumen::core::data;

// Batch 18 "Done when": two PCs exchange a plan with no internet connection.
// The second PC is a second process with a library of its own, sharing over
// TCP on this machine; the plan goes both ways. CTest also runs this test in
// a network namespace with no network but loopback (tst_sharing_offline).
namespace {

std::unique_ptr<Database> sampleLibrary()
{
    auto db = Database::open(Database::inMemory());
    if (!db.ok() || !seedSampleService(*db.value()).ok())
        return nullptr;
    return std::move(db).value();
}

QStringList titles(const PlanView &plan)
{
    QStringList out;
    for (const ItemView &item : plan.items)
        out << item.title;
    return out;
}

// The other PC: shares its library (the sample service plus "Youth Night")
// and writes its port and code to a file. Ends when the test does.
// Whether a multicast datagram sent from this machine comes back to it.
// Some networks join the group but never deliver (CI's macOS runners).
bool multicastLoopsBack(quint16 port)
{
    const QHostAddress group(QStringLiteral("239.255.76.77"));
    QUdpSocket socket;
    if (!socket.bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)
        || !socket.joinMulticastGroup(group))
        return false;
    socket.setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    for (int attempt = 0; attempt < 3; ++attempt) {
        socket.writeDatagram("lumen-probe", group, port);
        if (socket.waitForReadyRead(700))
            return true;
    }
    return false;
}

// The other computer quits when the test that started it has gone.
bool processAlive(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!process)
        return false;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
#else
    return ::kill(pid_t(pid), 0) == 0;
#endif
}

int otherComputer(const QString &infoFile, qint64 parentPid)
{
    auto db = sampleLibrary();
    if (!db)
        return 2;
    auto youth = plans::create(*db, QStringLiteral("Youth Night"), QDate(2026, 12, 4));
    if (!youth.ok() || !plans::addHeader(*db, youth.value().id, 0, QStringLiteral("Games")).ok())
        return 3;
    const auto all = Repository<Presentation>(*db).list();
    for (const Presentation &p : all.value()) {
        if (p.title == QLatin1String("Amazing Grace"))
            (void)Repository<PlanItem>(*db).create({.planId = youth.value().id, .position = 1, .presentationId = p.id});
    }
    QTemporaryDir media;
    Sharing sharing(*db, QStringLiteral("Booth PC"), media.path());
    sharing.setEnabled(true);
    if (!sharing.enabled())
        return 4;
    QFile info(infoFile);
    if (!info.open(QIODevice::WriteOnly))
        return 5;
    info.write(QStringLiteral("%1 %2\n").arg(sharing.port()).arg(sharing.code()).toUtf8());
    info.close();
    QTimer watch;
    QObject::connect(&watch, &QTimer::timeout, [parentPid] {
        if (!processAlive(parentPid))
            QCoreApplication::quit();
    });
    watch.start(500);
    QTimer::singleShot(120000, &QCoreApplication::quit);
    return QCoreApplication::exec();
}

} // namespace

class TestSharing : public QObject
{
    Q_OBJECT

private:
    QProcess m_other;
    QTemporaryDir m_dir;
    quint16 m_port = 0;
    QString m_code;

private slots:
    void initTestCase()
    {
        const QString info = m_dir.filePath(QStringLiteral("other.txt"));
        m_other.setProcessChannelMode(QProcess::ForwardedChannels);
        m_other.start(QCoreApplication::applicationFilePath(),
                      {QStringLiteral("--other-computer"), info, QString::number(QCoreApplication::applicationPid())});
        QVERIFY(m_other.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(info) && QFileInfo(info).size() > 0, 10000);
        QFile file(info);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList parts = QString::fromUtf8(file.readAll()).trimmed().split(u' ');
        QCOMPARE(parts.size(), 2);
        m_port = quint16(parts[0].toInt());
        m_code = parts[1];
        QVERIFY(m_port > 0);
        QCOMPARE(m_code.size(), 6);
    }

    void cleanupTestCase()
    {
        m_other.kill();
        m_other.waitForFinished();
    }

    // Done when: this PC copies a plan from the other, and sends one back.
    void plansGoBothWays()
    {
        auto mine = Database::open(Database::inMemory());
        QVERIFY(mine.ok());
        QTemporaryDir media;
        Sharing sharing(*mine.value(), QStringLiteral("Laptop"), media.path());
        const QString address = QStringLiteral("127.0.0.1:%1").arg(m_port);

        // The wrong code shows nothing.
        QVERIFY(!sharing.connectToAddress(address, QStringLiteral("000000") == m_code ? QStringLiteral("111111")
                                                                                      : QStringLiteral("000000")));
        QVERIFY2(sharing.status().contains(QStringLiteral("code")), qPrintable(sharing.status()));
        QCOMPARE(sharing.remotePlans()->property("count").toInt(), 0);

        // The right one lists its plans and songs.
        QVERIFY2(sharing.connectToAddress(address, m_code), qPrintable(sharing.status()));
        QVERIFY(sharing.status().contains(QStringLiteral("Booth PC")));
        auto *remotePlans = qobject_cast<RowModel *>(sharing.remotePlans());
        int youth = -1;
        for (int i = 0; i < remotePlans->rowCount(); ++i)
            youth = remotePlans->get(i).value("name").toString() == QLatin1String("Youth Night") ? i : youth;
        QVERIFY(youth >= 0);
        auto *remoteSongs = qobject_cast<RowModel *>(sharing.remoteSongs());
        QVERIFY(remoteSongs->rowCount() >= 3);

        // Copy it: the plan arrives with its header and song.
        QSignalSpy imported(&sharing, &Sharing::imported);
        QVERIFY2(sharing.copyPlan(youth), qPrintable(sharing.status()));
        QCOMPARE(imported.size(), 1);
        auto copy = loadPlan(*mine.value(), sharing.lastPlanId());
        QVERIFY(copy.ok());
        QCOMPARE(copy.value().name, QStringLiteral("Youth Night"));
        QCOMPARE(titles(copy.value()), (QStringList{"Games", "Amazing Grace"}));
        QVERIFY(!copy.value().items[1].slides.empty());

        // Copy two songs without a plan.
        QVERIFY2(sharing.copySongs({0, 1}), qPrintable(sharing.status()));
        QCOMPARE(int(Repository<Plan>(*mine.value()).list().value().size()), 1); // no new plan

        // Send a plan of this computer's own to the other.
        auto mineOwn = plans::create(*mine.value(), QStringLiteral("Sent from the laptop"), QDate(2026, 12, 6));
        QVERIFY(mineOwn.ok());
        QVERIFY(plans::addHeader(*mine.value(), mineOwn.value().id, 0, QStringLiteral("Welcome")).ok());
        QVERIFY2(sharing.sendPlan(mineOwn.value().id), qPrintable(sharing.status()));
        QVERIFY2(sharing.connectToAddress(address, m_code), qPrintable(sharing.status()));
        bool arrived = false;
        for (int i = 0; i < remotePlans->rowCount(); ++i)
            arrived = arrived || remotePlans->get(i).value("name").toString() == QLatin1String("Sent from the laptop");
        QVERIFY(arrived);
    }

    void wrongCodesAreLimited()
    {
        auto library = sampleLibrary();
        QVERIFY(library);
        QTemporaryDir media;
        Sharing server(*library, QStringLiteral("Office"), media.path());
        server.setEnabled(true);
        QVERIFY(server.enabled());
        const QString address = QStringLiteral("127.0.0.1:%1").arg(server.port());
        auto other = Database::open(Database::inMemory());
        Sharing client(*other.value(), QStringLiteral("Guest"), media.path());
        const QString wrong = server.code() == QLatin1String("123456") ? QStringLiteral("654321") : QStringLiteral("123456");
        for (int i = 0; i < 5; ++i)
            QVERIFY(!client.connectToAddress(address, wrong));
        // Now even the right code waits a minute.
        QVERIFY(!client.connectToAddress(address, server.code()));
        QVERIFY2(client.status().contains(QStringLiteral("wait")), qPrintable(client.status()));

        // Sharing off: nothing answers.
        server.setEnabled(false);
        QVERIFY(server.code().isEmpty());
        QVERIFY(!client.connectToAddress(address, wrong));
    }

    // Computers find each other on the local network (multicast). Skipped
    // where the network has no multicast (e.g. loopback only).
    void computersFindEachOther()
    {
        Sharing::Options options;
        options.discoveryPort = quint16(46000 + QCoreApplication::applicationPid() % 1000);
        auto a = sampleLibrary();
        auto b = sampleLibrary();
        QTemporaryDir media;
        Sharing first(*a, QStringLiteral("Sound desk"), media.path(), options);
        Sharing second(*b, QStringLiteral("Booth"), media.path(), options);
        first.setEnabled(true);
        second.discover(); // looking, without sharing itself
        if (first.status().contains(QStringLiteral("cannot be found")))
            QSKIP("no multicast on this network");
        if (!multicastLoopsBack(quint16(options.discoveryPort + 1)))
            QSKIP("multicast is not delivered on this network");
        auto *peers = qobject_cast<RowModel *>(second.peers());
        QTRY_VERIFY_WITH_TIMEOUT(peers->rowCount() > 0, 8000);
        QCOMPARE(peers->get(0).value("name").toString(), QStringLiteral("Sound desk"));
        // The one found can be connected to with its code.
        QVERIFY2(second.connectTo(0, first.code()), qPrintable(second.status()));
        QVERIFY(second.remotePlans()->property("count").toInt() >= 1);
        // A computer that is not sharing is not announced.
        auto *firstPeers = qobject_cast<RowModel *>(first.peers());
        QTest::qWait(2500);
        QCOMPARE(firstPeers->rowCount(), 0);
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc == 4 && qstrcmp(argv[1], "--other-computer") == 0)
        return otherComputer(QString::fromLocal8Bit(argv[2]), QByteArray(argv[3]).toLongLong());
    TestSharing test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_sharing.moc"
