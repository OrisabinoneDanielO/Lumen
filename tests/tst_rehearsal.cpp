#include "support/timing.h"
#include "operatorharness.h"

#include "lumen/core/render/renderlink.h"
#include "lumen/core/show/outputbridge.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QUuid>

using namespace lumen;
using namespace lumen::app;
using namespace lumen::core::render;
using namespace lumen::core::show;

// Batch 12: the automated stand-in for the MVP gate (the gate itself is 10
// pilot churches for 4 Sundays with zero output failures, which only people
// can run). The real operator window drives a real lumen-render process
// through a whole service, over and over:
//
//   pre-service countdown and a running timer in the banner, a motion
//   background, every item of the sample service slide by slide (songs,
//   scripture, announcements, sermon), a video, an imported PowerPoint deck,
//   a message, Logo, Black and Clear All.
//
// An output failure is any change the renderer has not put on screen within
// a second, the renderer stopping or restarting, or a dropped video frame.
// LUMEN_REHEARSAL_SECONDS sets the length (default 90; a full service-length
// run uses 3600).
class TestRehearsal : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<test::OperatorHarness> m_ui;
    std::unique_ptr<RenderLink> m_link;
    std::unique_ptr<OutputBridge> m_bridge;
    QStringList m_failures;
    int m_changes = 0;
    qint64 m_slowestMs = 0;
    qint64 m_pid = 0;

    QJsonObject status()
    {
        auto reply = m_link->request({{"cmd", "status"}}, 2000);
        return reply.ok() ? reply.value() : QJsonObject();
    }

    // Waits until the renderer shows the current scene; notes a failure if
    // it has not within a second.
    void expectOnScreen(const QString &what)
    {
        ++m_changes;
        const quint64 sequence = m_ui->show.scene().sequence;
        QElapsedTimer since;
        since.start();
        QDeadlineTimer deadline(1000);
        while (!deadline.hasExpired()) {
            const QJsonObject s = status();
            if (s.isEmpty())
                break;
            if (s.value("pid").toInteger() != m_pid) {
                m_failures << QStringLiteral("%1: the renderer restarted").arg(what);
                return;
            }
            if (quint64(s.value("presentedSequence").toInteger()) >= sequence) {
                m_slowestMs = std::max(m_slowestMs, since.elapsed());
                return;
            }
            QTest::qWait(5);
        }
        m_failures << QStringLiteral("%1: not on screen after 1 s").arg(what);
    }

    void pace(int ms) { QTest::qWait(ms); }

private slots:
    void initTestCase()
    {
        m_ui = std::make_unique<test::OperatorHarness>();
        QString error;
        QVERIFY2(m_ui->load(false, &error), qPrintable(error));
        // Media and a deck, as a church would have.
        MediaImportReport media = m_ui->media->importFiles(
            {QStringLiteral(LUMEN_MEDIA_DIR "/loop-1080p-vp9.webm"), QStringLiteral(LUMEN_MEDIA_DIR "/clip-360p-h264.mp4")});
        QCOMPARE(media.imported, 2);
        m_ui->controller->attachMedia(*m_ui->media);
        DeckImporter::Options office;
        office.office = DeckImporter::Office::Never;
        QCOMPARE(m_ui->decks->importFiles({QStringLiteral(LUMEN_DECKS_DIR "/order-16x9.pptx")}, office).imported, 1);
        m_ui->controller->attachPresentations(*m_ui->decks, office);

        RenderLink::Options options;
        options.serverName = QStringLiteral("lumen-rehearsal-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        options.executable = QStringLiteral(LUMEN_RENDER_PATH);
        options.arguments = {QStringLiteral("--windowed"), QStringLiteral("--watch-pid=%1").arg(QCoreApplication::applicationPid())};
        m_link = std::make_unique<RenderLink>(options);
        auto started = m_link->start();
        QVERIFY2(started.ok(), started.ok() ? "" : qPrintable(started.error().message));
        m_bridge = std::make_unique<OutputBridge>(m_ui->show, *m_link);
        QVERIFY(m_bridge->attach().ok());
        m_pid = status().value("pid").toInteger();
        QVERIFY(m_pid > 0);
    }

    void cleanupTestCase()
    {
        m_bridge.reset();
        if (m_link)
            (void)m_link->quit();
        m_link.reset();
        m_ui.reset();
    }

    void serviceRunsWithoutOutputFailures()
    {
        LUMEN_TIMING_CHECK();
        const int seconds = qEnvironmentVariableIntValue("LUMEN_REHEARSAL_SECONDS") > 0
                                ? qEnvironmentVariableIntValue("LUMEN_REHEARSAL_SECONDS") : 90;
        OperatorController &op = *m_ui->controller;
        TimerController &timers = *m_ui->timers;
        auto *plan = qobject_cast<RowModel *>(op.plan());
        auto *mediaRows = qobject_cast<RowModel *>(op.media());
        int loopRow = -1, clipRow = -1;
        for (int i = 0; i < mediaRows->rowCount(); ++i) {
            const QString title = mediaRows->get(i).value("title").toString();
            loopRow = title.startsWith(QLatin1String("loop")) ? i : loopRow;
            clipRow = title.startsWith(QLatin1String("clip")) ? i : clipRow;
        }
        QVERIFY(loopRow >= 0 && clipRow >= 0);
        int elapsedTimer = -1;
        for (int i = 0; i < qobject_cast<RowModel *>(timers.timers())->rowCount(); ++i)
            elapsedTimer = qobject_cast<RowModel *>(timers.timers())->get(i).value("name") == QLatin1String("Elapsed") ? i : elapsedTimer;

        QElapsedTimer clock;
        clock.start();
        int rounds = 0;
        while (clock.elapsed() < qint64(seconds) * 1000) {
            ++rounds;
            // Before the service: countdown slide, a clock in the banner.
            op.selectItem(0);
            QVERIFY(op.goLive(0));
            expectOnScreen(QStringLiteral("countdown slide"));
            QVERIFY(timers.reset(elapsedTimer));
            QVERIFY(timers.start(elapsedTimer));
            QVERIFY(timers.show(elapsedTimer));
            expectOnScreen(QStringLiteral("timer banner"));
            pace(500);
            timers.hide();
            expectOnScreen(QStringLiteral("timer hidden"));

            // A motion background under everything that follows.
            QVERIFY(op.useAsBackground(loopRow));
            expectOnScreen(QStringLiteral("motion background"));

            // Every item in the plan, slide by slide.
            for (int item = 1; item < plan->rowCount(); ++item) {
                op.selectItem(item);
                const int slides = op.slides()->property("count").toInt();
                for (int slide = 0; slide < slides; ++slide) {
                    QVERIFY(op.goLive(slide));
                    expectOnScreen(QStringLiteral("%1 slide %2").arg(op.itemTitle()).arg(slide + 1));
                    pace(150);
                }
                if (item == 3) {
                    // A message during the songs.
                    QVERIFY(op.showMessage(QStringLiteral("Parent of 42, please come to the nursery")));
                    expectOnScreen(QStringLiteral("message"));
                    pace(300);
                    op.hideMessage();
                    expectOnScreen(QStringLiteral("message hidden"));
                }
            }

            // A video, then the deck.
            QVERIFY(op.showMedia(clipRow));
            QVERIFY(op.goLive(0));
            expectOnScreen(QStringLiteral("video"));
            pace(1500);
            QVERIFY(op.showDeck(0));
            for (int slide = 0; slide < op.slides()->property("count").toInt(); ++slide) {
                QVERIFY(op.goLive(slide));
                expectOnScreen(QStringLiteral("deck slide %1").arg(slide + 1));
                pace(150);
            }

            // Logo, Black and Clear All at the end.
            op.toggleLogo();
            expectOnScreen(QStringLiteral("logo on"));
            op.toggleLogo();
            expectOnScreen(QStringLiteral("logo off"));
            op.toggleBlack();
            expectOnScreen(QStringLiteral("black on"));
            op.toggleBlack();
            expectOnScreen(QStringLiteral("black off"));
            op.clearAll();
            expectOnScreen(QStringLiteral("clear all"));
            if (m_failures.size() > 20)
                break;
        }

        const QJsonObject final = status();
        const QJsonObject video = final.value("video").toObject();
        const qint64 dropped = video.value("dropped").toInteger();
        qInfo("rehearsal: %d rounds in %lld s, %d changes, slowest on screen %lld ms, %lld video frames, %lld dropped, "
              "%d failures",
              rounds, clock.elapsed() / 1000, m_changes, m_slowestMs, video.value("frames").toInteger(), dropped,
              int(m_failures.size()));
        QVERIFY2(m_failures.isEmpty(), qPrintable(m_failures.mid(0, 10).join(QStringLiteral("; "))));
        QCOMPARE(final.value("pid").toInteger(), m_pid);
        QVERIFY(video.value("frames").toInteger() > 0);
        QCOMPARE(dropped, qint64(0));
    }
};

QTEST_MAIN(TestRehearsal)
#include "tst_rehearsal.moc"
