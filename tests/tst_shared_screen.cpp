#include "sharedscreen.h"
#include "sharing.h"

#include "lumen/core/data/database.h"

#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace lumen;
using namespace lumen::app;
using namespace lumen::core::show;

// Two operators, one screen: a laptop sends what goes live on it to the
// screen of the computer driving the projector, over the sharing
// connection (same code). Both computers run here, on loopback.
class TestSharedScreen : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<core::data::Database> m_db;
    std::unique_ptr<Sharing> m_sharing;
    std::unique_ptr<ScreenHost> m_host;
    ShowController m_hostShow;   // the computer driving the projector
    ShowController m_laptopShow; // the second operator's
    std::unique_ptr<ScreenLink> m_link;
    QTemporaryDir m_dir;

    QString slideText(const ShowController &show) const { return show.scene().layer(Layer::Slide).content.text; }

private slots:
    void init()
    {
        auto db = core::data::Database::open(core::data::Database::inMemory());
        QVERIFY(db.ok());
        m_db = std::move(db).value();
        Sharing::Options options;
        options.discoveryPort = quint16(46000 + QRandomGenerator::global()->bounded(1000));
        m_sharing = std::make_unique<Sharing>(*m_db, QStringLiteral("Church PC"), m_dir.filePath("imported"), options);
        m_sharing->setEnabled(true);
        m_host = std::make_unique<ScreenHost>(m_hostShow, m_dir.filePath("screen-files"));
        m_sharing->setScreenHandler([this](const QString &cmd, const QJsonObject &request, const QByteArray &payload) {
            return m_host->handle(cmd, request, payload);
        });
        m_hostShow.replaceScene(Scene{});
        m_laptopShow.replaceScene(Scene{});
        m_link = std::make_unique<ScreenLink>(m_laptopShow);
        m_link->setPollInterval(100);
    }
    void cleanup()
    {
        m_link.reset();
        m_host.reset();
        m_sharing.reset();
        m_db.reset();
        m_hostShow.clearAll();
        m_hostShow.setBlack(false);
        m_laptopShow.clearAll();
        m_laptopShow.setBlack(false);
    }

    // Off by default: the screen refuses until its operator allows it.
    void refusedUntilAllowed()
    {
        QVERIFY(m_link->start(QHostAddress::LocalHost, m_sharing->port(), m_sharing->code(), QStringLiteral("Church PC")));
        QTRY_VERIFY(m_link->status().contains(QStringLiteral("does not take a screen")));
        m_laptopShow.setContent(Layer::Slide, Content::textContent(QStringLiteral("Amazing grace")));
        QTest::qWait(300);
        QVERIFY(slideText(m_hostShow).isEmpty());
        // A wrong code: refused too.
        m_sharing->setTakesScreen(true);
        m_link->stop();
        QVERIFY(m_link->start(QHostAddress::LocalHost, m_sharing->port(), QStringLiteral("000000"), QString()));
        QTRY_VERIFY(m_link->status().contains(QStringLiteral("not its code")));
        QVERIFY(slideText(m_hostShow).isEmpty());
    }

    // What goes live on the laptop shows on the screen; what the screen's
    // operator does shows on the laptop.
    void bothOperatorsShareTheScreen()
    {
        m_sharing->setTakesScreen(true);
        m_hostShow.setContent(Layer::Background, Content::fill(Qt::darkBlue)); // already up there
        QVERIFY(m_link->start(QHostAddress::LocalHost, m_sharing->port(), m_sharing->code(), QStringLiteral("Church PC")));
        QTRY_VERIFY(m_link->status().contains(QStringLiteral("Live goes to Church PC")));
        // Joining takes on what the screen shows.
        QTRY_COMPARE(m_laptopShow.scene().layer(Layer::Background).content, Content::fill(Qt::darkBlue));

        m_laptopShow.setContent(Layer::Slide, Content::textContent(QStringLiteral("John 3:16")));
        QTRY_COMPARE(slideText(m_hostShow), QStringLiteral("John 3:16"));
        QCOMPARE(m_hostShow.scene().layer(Layer::Background).content, Content::fill(Qt::darkBlue)); // untouched

        // The screen's operator goes to black and puts up a song: the
        // laptop follows.
        m_hostShow.setBlack(true);
        m_hostShow.setContent(Layer::Slide, Content::textContent(QStringLiteral("Amazing grace")));
        QTRY_VERIFY(m_laptopShow.scene().black);
        QTRY_COMPARE(slideText(m_laptopShow), QStringLiteral("Amazing grace"));
        // Black off from the laptop.
        m_laptopShow.setBlack(false);
        QTRY_VERIFY(!m_hostShow.scene().black);
        QCOMPARE(slideText(m_hostShow), QStringLiteral("Amazing grace")); // not sent back over it

        // Stopped: the laptop's changes stay on the laptop.
        m_link->stop();
        m_laptopShow.setContent(Layer::Slide, Content::textContent(QStringLiteral("Only here")));
        QTest::qWait(300);
        QCOMPARE(slideText(m_hostShow), QStringLiteral("Amazing grace"));
    }

    // A video or picture from the laptop is copied to the screen once.
    void mediaTravelsOnce()
    {
        m_sharing->setTakesScreen(true);
        QVERIFY(m_link->start(QHostAddress::LocalHost, m_sharing->port(), m_sharing->code(), QStringLiteral("Church PC")));
        QTRY_VERIFY(m_link->status().contains(QStringLiteral("Live goes to")));
        const QString picture = QStringLiteral(LUMEN_MEDIA_DIR "/bars-720p.png");
        m_laptopShow.setContent(Layer::Media, Content::image(picture));
        QTRY_VERIFY(!m_hostShow.scene().layer(Layer::Media).content.imagePath.isEmpty());
        const QString there = m_hostShow.scene().layer(Layer::Media).content.imagePath;
        QVERIFY(there.startsWith(m_dir.filePath("screen-files")));
        QCOMPARE(QFileInfo(there).size(), QFileInfo(picture).size());
        // The laptop shows it from its own file.
        QTRY_COMPARE(m_laptopShow.scene().layer(Layer::Media).content.imagePath, picture);
        // Again later: no second copy.
        m_laptopShow.clear(Layer::Media);
        QTRY_VERIFY(m_hostShow.scene().layer(Layer::Media).content.isEmpty());
        QFile::remove(there); // gone from the screen's folder: asked for again
        m_laptopShow.setContent(Layer::Media, Content::image(picture));
        QTRY_COMPARE(m_hostShow.scene().layer(Layer::Media).content.imagePath, there);
        QVERIFY(QFileInfo::exists(there)); // asked for again, since it was gone
    }

    // Music from the laptop plays on the screen's computer (copied once).
    void audioTravels()
    {
        m_sharing->setTakesScreen(true);
        QVERIFY(m_link->start(QHostAddress::LocalHost, m_sharing->port(), m_sharing->code(), QStringLiteral("Church PC")));
        QTRY_VERIFY(m_link->status().contains(QStringLiteral("Live goes to")));
        const QString tone = QStringLiteral(LUMEN_MEDIA_DIR "/tone-2s.wav");
        m_laptopShow.setAudio(Content::video(tone, true, 1.0));
        QTRY_VERIFY(!m_hostShow.scene().audio.isEmpty());
        QVERIFY(m_hostShow.scene().audio.videoPath.startsWith(m_dir.filePath("screen-files")));
        QVERIFY(m_hostShow.scene().audio.loop);
        m_laptopShow.setAudio(Content::empty());
        QTRY_VERIFY(m_hostShow.scene().audio.isEmpty());
    }

    // Pause and seek from the laptop reach the screen's output.
    void videoControlReachesTheScreen()
    {
        m_sharing->setTakesScreen(true);
        QSignalSpy video(m_host.get(), &ScreenHost::videoControlRequested);
        QVERIFY(m_link->start(QHostAddress::LocalHost, m_sharing->port(), m_sharing->code(), QStringLiteral("Church PC")));
        QTRY_VERIFY(m_link->status().contains(QStringLiteral("Live goes to")));
        m_link->videoControl(QStringLiteral("seek"), 4000);
        QTRY_COMPARE(video.count(), 1);
        QCOMPARE(video.at(0).at(0).toString(), QStringLiteral("seek"));
        QCOMPARE(video.at(0).at(1).toLongLong(), 4000);
    }
};

QTEST_MAIN(TestSharedScreen)
#include "tst_shared_screen.moc"
