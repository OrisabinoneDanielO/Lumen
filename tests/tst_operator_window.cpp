#include "operatorharness.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include "backups.h"
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>

using namespace lumen;
using namespace lumen::core::show;
using lumen::test::OperatorHarness;

// The operator window driven like an operator would: mouse clicks and keys.
class TestOperatorWindow : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<OperatorHarness> m_ui;

    QString onAir() const
    {
        const Content &content = m_ui->show.scene().layer(Layer::Slide).content;
        return content.kind == Content::Kind::Text ? content.text : QString();
    }

    void open(bool editAvailable = false)
    {
        m_ui = std::make_unique<OperatorHarness>();
        QString error;
        QVERIFY2(m_ui->load(editAvailable, &error), qPrintable(error));
    }

private slots:
    void init()
    {
        // Any warning fails, except Qt's note that a missing font family
        // made font lookup slow (macOS, where "Sans Serif" does not exist).
        QTest::failOnWarning(QRegularExpression(QStringLiteral("^(?!Populating font family aliases took ).*")));
    }

    void cleanup() { m_ui.reset(); }

    void clickingAPlanItemShowsItsSlides()
    {
        open();
        QVERIFY(m_ui->click("planItem-2"));
        QCOMPARE(m_ui->controller->currentItem(), 2);
        QCOMPARE(m_ui->controller->itemTitle(), QStringLiteral("Amazing Grace"));
        QTRY_VERIFY(m_ui->item("slide-8"));
        QCOMPARE(m_ui->show.scene().sequence, quint64(0)); // nothing went live
    }

    void clickingASlideSendsItLive()
    {
        open();
        m_ui->click("planItem-2");
        QTRY_VERIFY(m_ui->item("slide-2"));
        QVERIFY(m_ui->click("slide-2"));
        QVERIFY(onAir().startsWith(QLatin1String("'Twas grace")));
        QTRY_COMPARE(m_ui->item("slide-2")->property("status").toString(), QStringLiteral("live"));
        QCOMPARE(m_ui->item("slide-3")->property("status").toString(), QStringLiteral("next"));
        QTRY_COMPARE(m_ui->item("liveFrame")->property("text").toString(), onAir());
    }

    void keysDriveTheShow()
    {
        open();
        m_ui->click("planItem-2");
        m_ui->key(Qt::Key_Space);
        QVERIFY(onAir().startsWith(QLatin1String("Amazing grace!")));
        m_ui->key(Qt::Key_Right);
        QVERIFY(onAir().startsWith(QLatin1String("I once was lost")));
        m_ui->key(Qt::Key_Left);
        QVERIFY(onAir().startsWith(QLatin1String("Amazing grace!")));
        m_ui->key(Qt::Key_V);
        m_ui->key(Qt::Key_3);
        QVERIFY(onAir().startsWith(QLatin1String("Through many dangers")));
        m_ui->key(Qt::Key_F7);
        QVERIFY(m_ui->controller->blackOn());
        m_ui->key(Qt::Key_F7);
        m_ui->key(Qt::Key_PageDown);
        QCOMPARE(m_ui->controller->currentItem(), 3);
    }

    void toolbarButtonsWork()
    {
        open();
        m_ui->click("planItem-2");
        m_ui->key(Qt::Key_Space);
        QVERIFY(m_ui->click("black"));
        QVERIFY(m_ui->show.scene().black);
        QVERIFY(m_ui->click("black"));
        QVERIFY(m_ui->click("logo"));
        QVERIFY(m_ui->show.scene().logo);
        QVERIFY(m_ui->click("clearSlide"));
        QVERIFY(onAir().isEmpty());
        QVERIFY(m_ui->click("nextSlide"));
        QVERIFY(onAir().startsWith(QLatin1String("I once was lost")));
        QVERIFY(m_ui->click("previousSlide"));
        QVERIFY(onAir().startsWith(QLatin1String("Amazing grace!")));
        QVERIFY(m_ui->click("clearAll"));
        QVERIFY(onAir().isEmpty());
    }

    // Typing in the library search must never change the screen: letters,
    // numbers and Space are typing there. The F-keys (Clear, Logo, Black)
    // are not typing and always work, so the operator can black the screen
    // from anywhere. (Until the QA pass of 2026-11-26 F7 was blocked here
    // too; changed because a safety key that sometimes does nothing is
    // unsafe. See docs/decisions.md.)
    void shortcutsPauseWhileSearching()
    {
        open();
        m_ui->click("planItem-2");
        QVERIFY(m_ui->click("librarySearch"));
        QTRY_VERIFY(m_ui->item("searchInput")->hasActiveFocus());
        for (const char c : QByteArray("grace c 3"))
            QTest::keyClick(m_ui->window, c);
        QCOMPARE(m_ui->show.scene().sequence, quint64(0));
        QCOMPARE(m_ui->item("searchInput")->property("text").toString(), QStringLiteral("grace c 3"));
        m_ui->key(Qt::Key_F7);
        QVERIFY(m_ui->show.scene().black);
        QCOMPARE(m_ui->item("searchInput")->property("text").toString(), QStringLiteral("grace c 3"));
        m_ui->key(Qt::Key_F7);
        QVERIFY(!m_ui->show.scene().black);

        m_ui->key(Qt::Key_Escape); // back to the show
        QTRY_VERIFY(!m_ui->item("searchInput")->hasActiveFocus());
        m_ui->key(Qt::Key_Space);
        QVERIFY(onAir().startsWith(QLatin1String("Amazing grace!")));
    }

    // Closing a popover, or clicking a library card or row, gives the
    // keyboard back to the show; Escape closes the open popover.
    void keyboardComesBack()
    {
        open();
        m_ui->click("planItem-2");
        QVERIFY(m_ui->click("timersButton"));
        QTRY_VERIFY(m_ui->item("timersPopover")->property("active").toBool());
        m_ui->key(Qt::Key_Escape);
        QTRY_VERIFY(!m_ui->item("timersPopover")->property("active").toBool());
        m_ui->key(Qt::Key_Space);
        QVERIFY(onAir().startsWith(QLatin1String("Amazing grace!")));

        // A song row clicked after searching: Space is the show's again.
        QVERIFY(m_ui->click("librarySearch"));
        QTRY_VERIFY(m_ui->item("searchInput")->hasActiveFocus());
        QVERIFY(m_ui->clickSongRow(QStringLiteral("Holy, Holy, Holy")));
        QTRY_VERIFY(!m_ui->item("searchInput")->hasActiveFocus());
        m_ui->key(Qt::Key_Space);
        QVERIFY2(onAir().startsWith(QLatin1String("Holy, holy, holy")), qPrintable(onAir()));
    }

    // A new (empty) plan leaves no old slides in the grid. (Edit mode and
    // Use as background: tst_media, which handles real videos.)
    void emptyPlanClearsTheGrid()
    {
        open(true);
        m_ui->click("planItem-2");
        QVERIFY(m_ui->controller->slides()->property("count").toInt() > 0);
        QVERIFY(m_ui->controller->newPlan(QStringLiteral("Empty")));
        QCOMPARE(m_ui->controller->slides()->property("count").toInt(), 0);
    }

    // Restoring a backup asks first and says what the backup holds; only
    // "Restore and restart" restarts.
    void restoreIsConfirmed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("library.db"));
        auto db = core::data::Database::open(path);
        QVERIFY(db.ok());
        QVERIFY(app::seedSampleService(*db.value()).ok());
        m_ui = std::make_unique<OperatorHarness>();
        QString error;
        QVERIFY2(m_ui->load(false, &error, std::move(db).value()), qPrintable(error));
        app::Backups backups(m_ui->database(), dir.filePath(QStringLiteral("backups")));
        QVERIFY(backups.backUpNow().ok());
        m_ui->controller->attachBackups(backups, path, dir.filePath(QStringLiteral("imported")));
        auto *rows = qobject_cast<app::RowModel *>(m_ui->controller->backups());
        QCOMPARE(rows->count(), 1);
        QVERIFY2(rows->get(0).value("contents").toString().contains(QStringLiteral("plan")),
                 qPrintable(rows->get(0).value("contents").toString()));

        QSignalSpy restart(m_ui->controller.get(), &app::OperatorController::restartRequested);
        QVERIFY(m_ui->click("planMenuButton"));
        QTRY_VERIFY(m_ui->item("planRestore"));
        QVERIFY(m_ui->click("planRestore"));
        QTRY_VERIFY(m_ui->item("backup-0") && m_ui->item("backup-0")->isVisible());
        QVERIFY(m_ui->click("backup-0"));
        QTRY_VERIFY(m_ui->item("confirmRestore") && m_ui->item("confirmRestore")->isVisible());
        QCOMPARE(restart.count(), 0); // nothing yet
        QVERIFY(m_ui->click("cancelRestore"));
        QCOMPARE(restart.count(), 0);

        QVERIFY(m_ui->click("planMenuButton"));
        QVERIFY(m_ui->click("planRestore"));
        QVERIFY(m_ui->click("backup-0"));
        QVERIFY(m_ui->click("confirmRestore"));
        QCOMPARE(restart.count(), 1);
        m_ui.reset(); // before the backups and the folder go
    }

    // Live shows what the audience sees: Black and the Logo cover it, and it
    // says when the output is off.
    void liveShowsOverrides()
    {
        open();
        m_ui->controller->setOutputConnected(true);
        m_ui->click("planItem-2");
        m_ui->key(Qt::Key_Space);
        QQuickItem *cover = m_ui->item("liveOverride");
        QVERIFY(cover && !cover->isVisible());
        m_ui->key(Qt::Key_F7);
        QTRY_VERIFY(cover->isVisible());
        m_ui->key(Qt::Key_F7);
        QTRY_VERIFY(!cover->isVisible());
        QVERIFY(!m_ui->item("liveOutputOff")->isVisible());
        m_ui->controller->setOutputConnected(false);
        QTRY_VERIFY(m_ui->item("liveOutputOff")->isVisible());
    }

    // Page Down skips headers, and from a library song goes back to the plan
    // after the item that was live.
    void pageDownSkipsHeadersAndReturnsToThePlan()
    {
        open();
        m_ui->controller->selectItem(1);
        QVERIFY(m_ui->controller->addHeader(QStringLiteral("Worship"))); // item 2 now
        m_ui->controller->selectItem(1);
        m_ui->key(Qt::Key_PageDown);
        QCOMPARE(m_ui->controller->currentItem(), 3); // not the header
        m_ui->key(Qt::Key_PageUp);
        QCOMPARE(m_ui->controller->currentItem(), 1);

        m_ui->controller->selectItem(3);
        QVERIFY(m_ui->controller->goLive(0));
        QVERIFY(m_ui->controller->showSong(0)); // a library song, not in the plan
        QVERIFY(m_ui->controller->currentItem() < 0);
        m_ui->key(Qt::Key_PageDown);
        QCOMPARE(m_ui->controller->currentItem(), 4);
    }

    // Deleting the open plan asks first; the songs stay in the library.
    void deletePlanIsConfirmed()
    {
        open();
        const int songs = m_ui->controller->songs()->property("count").toInt();
        QVERIFY(!m_ui->controller->planId().isEmpty());
        QVERIFY(m_ui->click("planMenuButton"));
        QTRY_VERIFY(m_ui->item("planDelete") && m_ui->item("planDelete")->isVisible());
        QVERIFY(m_ui->click("planDelete"));
        QTRY_VERIFY(m_ui->item("confirmDeletePlan")->isVisible());
        QVERIFY(m_ui->click("cancelDeletePlan"));
        QVERIFY(!m_ui->controller->planId().isEmpty());
        QVERIFY(m_ui->click("planMenuButton"));
        QVERIFY(m_ui->click("planDelete"));
        QVERIFY(m_ui->click("confirmDeletePlan"));
        QTRY_VERIFY(m_ui->controller->planId().isEmpty());
        QCOMPARE(m_ui->controller->songs()->property("count").toInt(), songs);

        // Return in the library search: the first song found, in Preview.
        QVERIFY(m_ui->click("librarySearch"));
        for (const char c : QByteArray("holy"))
            QTest::keyClick(m_ui->window, c);
        QTest::keyClick(m_ui->window, Qt::Key_Return);
        QTRY_COMPARE(m_ui->controller->itemTitle(), QStringLiteral("Holy, Holy, Holy"));
        QTRY_VERIFY(!m_ui->item("searchInput")->hasActiveFocus());
    }

    // A video the output could not play: Live says so (from the output's
    // status), and stops saying so once the output no longer reports it.
    void liveSaysWhenAVideoFails()
    {
        open();
        // A video on the screen (no renderer in this test: only what Live says).
        m_ui->show.setContent(Layer::Media, Content::video(QStringLiteral("/media/offering.mp4"), false, 1.0));
        const Content media = m_ui->show.scene().layer(Layer::Media).content;
        QCOMPARE(media.kind, Content::Kind::Video);
        QVERIFY(m_ui->controller->liveProblem().isEmpty());
        QVERIFY(!m_ui->item("liveProblem")->isVisible());

        m_ui->controller->setOutputStatus(
            {{"videoErrors", QJsonArray{QJsonObject{{"path", media.videoPath}, {"message", "Could not open file"}}}}});
        QVERIFY(m_ui->controller->liveProblem().contains(QStringLiteral("Could not open file")));
        QVERIFY(m_ui->controller->liveProblem().contains(QFileInfo(media.videoPath).fileName()));
        QTRY_VERIFY(m_ui->item("liveProblem")->isVisible());
        m_ui->controller->setOutputStatus({});
        QTRY_VERIFY(!m_ui->item("liveProblem")->isVisible());
    }

    // The Main light asks before turning the output off.
    void outputOffIsConfirmed()
    {
        open();
        m_ui->controller->setOutputConnected(true);
        QSignalSpy toggled(m_ui->controller.get(), &app::OperatorController::outputToggleRequested);
        QVERIFY(m_ui->click("output-0"));
        QTRY_VERIFY(m_ui->item("outputConfirm")->isVisible());
        QCOMPARE(toggled.count(), 0);
        QVERIFY(m_ui->click("outputOffCancel"));
        QTRY_VERIFY(!m_ui->item("outputConfirm")->isVisible());
        QCOMPARE(toggled.count(), 0);
        QVERIFY(m_ui->click("output-0"));
        QVERIFY(m_ui->click("outputOffConfirm"));
        QCOMPARE(toggled.count(), 1);
        QCOMPARE(toggled.first().first().toBool(), false);
    }

    // The Transition box: pick how slides change and how long it takes;
    // the next slide sent live changes that way (and the choice is kept).
    void transitionPicker()
    {
        open();
        QVERIFY(m_ui->click("transitionBox"));
        QTRY_VERIFY(m_ui->item("transitionMenu")->isVisible());
        QVERIFY(m_ui->click("transition-push"));
        QCOMPARE(m_ui->controller->transition(), QStringLiteral("push"));
        QVERIFY(m_ui->click("transitionSpeed-1000"));
        QTRY_VERIFY(!m_ui->item("transitionMenu")->isVisible());
        QCOMPARE(m_ui->controller->transitionMs(), 1000);
        m_ui->controller->selectItem(2);
        QVERIFY(m_ui->controller->goLive(1));
        QCOMPARE(m_ui->show.scene().layer(Layer::Slide).transition, Transition::of(TransitionKind::Push, 1000));
        QCOMPARE(m_ui->show.scene().layer(Layer::Media).transition, Transition::of(TransitionKind::Push, 1000));
        // Back to a cut (kept for the next start, so leave it as found).
        QVERIFY(m_ui->click("transitionBox"));
        QVERIFY(m_ui->click("transition-cut"));
        QCOMPARE(m_ui->show.scene().layer(Layer::Slide).transition, Transition::cut());
        QTRY_VERIFY(!m_ui->item("transitionMenu")->isVisible());

        // Show message… beside it opens the message panel.
        QVERIFY(m_ui->click("showMessageButton"));
        QTRY_VERIFY(m_ui->item("messagePopover")->isVisible());
    }

    void editModeSwitchAndLockout()
    {
        open(true);
        m_ui->click("planItem-2");
        m_ui->key(Qt::Key_Space);
        const quint64 sequence = m_ui->show.scene().sequence;

        QVERIFY(m_ui->item("editMode")->isVisible());
        QVERIFY(m_ui->click("editMode"));
        QCOMPARE(m_ui->controller->mode(), QStringLiteral("edit"));
        QTRY_VERIFY(m_ui->item("editor")->isVisible());
        QVERIFY(!m_ui->item("slideGrid")->isVisible());

        QTRY_VERIFY(m_ui->item("editorSlide-2"));
        QVERIFY(m_ui->click("editorSlide-2")); // in view in the strip
        QCOMPARE(m_ui->controller->selectedSlide(), 2);
        m_ui->key(Qt::Key_Space);
        QCOMPARE(m_ui->show.scene().sequence, sequence); // nothing reached the screen

        QVERIFY(m_ui->click("showMode"));
        QTRY_VERIFY(m_ui->item("slideGrid")->isVisible());
        m_ui->key(Qt::Key_Space);
        QVERIFY(onAir().startsWith(QLatin1String("I once was lost")));
    }
};

QTEST_MAIN(TestOperatorWindow)
#include "tst_operator_window.moc"
