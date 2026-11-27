#include "operatorharness.h"

#include "backups.h"
#include "lumenfile.h"
#include "medialibrary.h"
#include "planstore.h"
#include "presentationlibrary.h"

#include "lumen/core/data/repository.h"

#include <QDirIterator>
#include <QProcess>
#include <QSignalSpy>

#include <cstdlib>

using namespace lumen;
using namespace lumen::app;
using namespace lumen::core::data;

// Batch 11 "Done when": a plan survives a crash and restores from backup.
// Also: plan editing, .lumen export and import, and the plan menu.
namespace {

QStringList titles(const PlanView &plan)
{
    QStringList out;
    for (const ItemView &item : plan.items)
        out << item.title;
    return out;
}

QString songId(Database &db, const QString &title)
{
    const auto all = Repository<Presentation>(db).list();
    for (const Presentation &p : all.value()) {
        if (p.title == title)
            return p.id;
    }
    return {};
}

// The child process for crashKeepsThePlan: builds a plan through the
// controller, as an operator would, saves the session as the 30-second
// autosave does, then crashes without closing anything.
int crashChild(const QString &libraryPath)
{
    auto db = Database::open(libraryPath);
    if (!db.ok() || !seedSampleService(*db.value()).ok())
        return 2;
    core::show::ShowController show;
    core::songs::SongLibrary songs(*db.value());
    OperatorController controller(show, false);
    controller.attachLibrary(*db.value(), songs);
    Backups backups(*db.value(), QFileInfo(libraryPath).dir().filePath(QStringLiteral("backups")));
    controller.attachBackups(backups, libraryPath, QFileInfo(libraryPath).dir().filePath(QStringLiteral("imported")));
    if (!controller.newPlan(QStringLiteral("Crash test"))
        || !controller.addPresentationToPlan(songId(*db.value(), QStringLiteral("Amazing Grace")))
        || !controller.addPresentationToPlan(songId(*db.value(), QStringLiteral("Holy, Holy, Holy")))
        || !controller.addHeader(QStringLiteral("Sermon"))
        || !controller.setItemNotes(0, QStringLiteral("Capo 2"))
        || !controller.moveItem(2, 0))
        return 3;
    controller.selectItem(2);
    controller.saveSession();
    std::abort(); // no destructors, no closing the database
}

} // namespace

class TestPlans : public QObject
{
    Q_OBJECT

private slots:
    void planEditing()
    {
        auto opened = Database::open(Database::inMemory());
        QVERIFY(opened.ok());
        Database &db = *opened.value();
        QVERIFY(seedSampleService(db).ok());
        auto plan = plans::create(db, QStringLiteral("Evening"), QDate(2026, 11, 29));
        QVERIFY(plan.ok());
        const QString id = plan.value().id;
        for (const char *title : {"Amazing Grace", "Holy, Holy, Holy", "Be Thou My Vision"}) {
            QVERIFY(Repository<PlanItem>(db)
                        .create({.planId = id, .position = int(Repository<PlanItem>(db).listChildren(id).value().size()),
                                 .presentationId = songId(db, QLatin1String(title))})
                        .ok());
        }
        QVERIFY(plans::addHeader(db, id, 1, QStringLiteral("Worship")).ok());
        QCOMPARE(titles(loadPlan(db, id).value()),
                 (QStringList{"Amazing Grace", "Worship", "Holy, Holy, Holy", "Be Thou My Vision"}));
        QVERIFY(plans::moveItem(db, id, 3, 0).ok());
        QCOMPARE(titles(loadPlan(db, id).value()),
                 (QStringList{"Be Thou My Vision", "Amazing Grace", "Worship", "Holy, Holy, Holy"}));
        QVERIFY(plans::removeItem(db, id, 1).ok());
        QVERIFY(plans::setNotes(db, id, 0, QStringLiteral("Key of D")).ok());
        const PlanView edited = loadPlan(db, id).value();
        QCOMPARE(titles(edited), (QStringList{"Be Thou My Vision", "Worship", "Holy, Holy, Holy"}));
        QCOMPARE(edited.items[0].notes, QStringLiteral("Key of D"));
        QVERIFY(edited.items[1].presentationId.isEmpty()); // a header
        // Positions stay 0, 1, 2.
        const auto items = Repository<PlanItem>(db).listChildren(id).value();
        for (int i = 0; i < int(items.size()); ++i)
            QCOMPARE(items[size_t(i)].position, i);

        QVERIFY(!plans::moveItem(db, id, 0, 5).ok());
        QVERIFY(!plans::addHeader(db, id, 0, QStringLiteral("  ")).ok());

        auto copy = plans::duplicate(db, id, QStringLiteral("Evening (copy)"), QDate(2026, 12, 6));
        QVERIFY(copy.ok());
        const PlanView copied = loadPlan(db, copy.value().id).value();
        QCOMPARE(titles(copied), titles(edited));
        QCOMPARE(copied.items[0].notes, QStringLiteral("Key of D"));

        auto all = plans::list(db);
        QVERIFY(all.ok());
        QCOMPARE(all.value().front().name, QStringLiteral("Evening (copy)")); // newest first
        QVERIFY(plans::remove(db, copy.value().id).ok());
        QVERIFY(loadPlan(db, id).ok()); // the original is untouched
    }

    // Done when (1): the plan survives a crash. A child process builds a
    // plan and aborts; everything it did is there afterwards, and the app
    // reopens the same plan and item.
    void crashKeepsThePlan()
    {
        QTemporaryDir folder;
        const QString library = folder.filePath(QStringLiteral("library.db"));
        QProcess child;
        child.setProcessChannelMode(QProcess::ForwardedChannels);
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--crash-child"), library});
        QVERIFY(child.waitForFinished(60000));
        QCOMPARE(child.exitStatus(), QProcess::CrashExit);

        auto db = Database::open(library);
        QVERIFY2(db.ok(), db.ok() ? "" : qPrintable(db.error().message));
        QVERIFY(db.value()->isIntact().value());
        core::show::ShowController show;
        core::songs::SongLibrary songs(*db.value());
        OperatorController controller(show, false);
        controller.attachLibrary(*db.value(), songs);
        Backups backups(*db.value(), folder.filePath(QStringLiteral("backups")));
        controller.attachBackups(backups, library, folder.filePath(QStringLiteral("imported")));
        QVERIFY(controller.restoreSession());
        QCOMPARE(controller.planName(), QStringLiteral("Crash test"));
        QCOMPARE(controller.currentItem(), 2);
        auto *plan = qobject_cast<RowModel *>(controller.plan());
        QCOMPARE(plan->rowCount(), 3);
        // Added: Amazing Grace, Holy, Holy, Holy; the header went after the
        // selected first item; then the last item moved to the top.
        QCOMPARE(plan->get(0).value("title").toString(), QStringLiteral("Holy, Holy, Holy"));
        QCOMPARE(plan->get(1).value("title").toString(), QStringLiteral("Amazing Grace"));
        QCOMPARE(plan->get(1).value("notes").toString(), QStringLiteral("Capo 2"));
        QCOMPARE(plan->get(2).value("title").toString(), QStringLiteral("Sermon"));
        QVERIFY(plan->get(2).value("header").toBool());
    }

    // Done when (2): the plan restores from a backup.
    void restoresFromBackup()
    {
        QTemporaryDir folder;
        const QString library = folder.filePath(QStringLiteral("library.db"));
        const QString backupFolder = folder.filePath(QStringLiteral("backups"));
        QString keptPlan;
        {
            auto db = Database::open(library);
            QVERIFY(db.ok());
            QVERIFY(seedSampleService(*db.value()).ok());
            auto plan = plans::create(*db.value(), QStringLiteral("Easter"), QDate(2027, 3, 28));
            QVERIFY(plan.ok());
            keptPlan = plan.value().id;
            QVERIFY(plans::addHeader(*db.value(), keptPlan, 0, QStringLiteral("Sunrise")).ok());

            Backups backups(*db.value(), backupFolder);
            auto made = backups.backUpNow();
            QVERIFY2(made.ok(), made.ok() ? "" : qPrintable(made.error().message));
            QCOMPARE(int(backups.list().size()), 1);
            QVERIFY(Repository<Library>(*db.value()).list().value().front().lastBackup.has_value());

            // Then the plan is lost (deleted by mistake) and another made.
            QVERIFY(plans::remove(*db.value(), keptPlan).ok());
            QVERIFY(plans::create(*db.value(), QStringLiteral("After the backup"), QDate(2027, 4, 4)).ok());
            QVERIFY(!Repository<Plan>(*db.value()).get(keptPlan).ok());

            QVERIFY(Backups::scheduleRestore(library, made.value().path).ok());
        } // the app quits

        // The next start puts the backup in place before opening.
        auto applied = Backups::applyPendingRestore(library);
        QVERIFY2(applied.ok(), applied.ok() ? "" : qPrintable(applied.error().message));
        QVERIFY(applied.value());
        QVERIFY(!Backups::applyPendingRestore(library).value()); // only once

        auto db = Database::open(library);
        QVERIFY(db.ok());
        auto restored = loadPlan(*db.value(), keptPlan);
        QVERIFY(restored.ok());
        QCOMPARE(restored.value().name, QStringLiteral("Easter"));
        QCOMPARE(titles(restored.value()), QStringList{QStringLiteral("Sunrise")});
        const auto restoredPlans = Repository<Plan>(*db.value()).list();
        for (const Plan &p : restoredPlans.value())
            QVERIFY(p.name != QLatin1String("After the backup"));

        // The replaced library is kept, with the newer plan in it.
        const QStringList kept = QDir(folder.path()).entryList({QStringLiteral("library-before-restore-*.db")});
        QCOMPARE(kept.size(), 1);
        auto before = Database::open(folder.filePath(kept.front()));
        QVERIFY(before.ok());
        bool found = false;
        const auto keptPlans = Repository<Plan>(*before.value()).list();
        for (const Plan &p : keptPlans.value())
            found = found || p.name == QLatin1String("After the backup");
        QVERIFY(found);
    }

    void backupsAreDailyAndPruned()
    {
        QTemporaryDir folder;
        auto db = Database::open(folder.filePath(QStringLiteral("library.db")));
        QVERIFY(db.ok());
        Backups backups(*db.value(), folder.filePath(QStringLiteral("backups")), 3);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(backups.backUpIfDue(now).value());              // none yet
        QVERIFY(!backups.backUpIfDue(now.addSecs(3600)).value()); // an hour later
        QVERIFY(backups.backUpIfDue(now.addSecs(25 * 3600)).value());
        for (int i = 0; i < 4; ++i) {
            QTest::qWait(1100); // backups are named by the second
            QVERIFY(backups.backUpNow().ok());
        }
        const auto entries = backups.list();
        QCOMPARE(int(entries.size()), 3);
        QVERIFY(entries[0].made >= entries[1].made);
        // A damaged file is refused for restoring.
        QFile junk(folder.filePath(QStringLiteral("backups/library-20260101-000000.db")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray(4096, 'x'));
        junk.close();
        QVERIFY(!Backups::scheduleRestore(folder.filePath(QStringLiteral("library.db")), junk.fileName()).ok());
    }

    void lumenFileRoundTrip()
    {
        QTemporaryDir folder;
        auto source = Database::open(Database::inMemory());
        QVERIFY(source.ok());
        Database &db = *source.value();
        QVERIFY(seedSampleService(db).ok());
        MediaLibrary media(db, folder.filePath(QStringLiteral("thumbs")));
        QCOMPARE(media.importFiles({QStringLiteral(LUMEN_MEDIA_DIR "/bars-720p.png")}).imported, 1);
        PresentationLibrary decks(db, folder.filePath(QStringLiteral("slides")));
        DeckImporter::Options options;
        options.office = DeckImporter::Office::Never;
        QCOMPARE(decks.importFiles({QStringLiteral(LUMEN_DECKS_DIR "/classic-4x3.pptx")}, options).imported, 1);

        auto plan = loadLatestPlan(db).value();
        QVERIFY(plan.has_value());
        const QString planId = plan->id;
        const auto mediaRows = media.list();
        for (const auto &row : mediaRows.value())
            QVERIFY(Repository<PlanItem>(db).create({.planId = planId, .position = 50, .presentationId = row.presentationId}).ok());
        const auto deckRows = decks.list();
        for (const auto &row : deckRows.value())
            QVERIFY(Repository<PlanItem>(db).create({.planId = planId, .position = 51, .presentationId = row.presentationId}).ok());
        QVERIFY(plans::addHeader(db, planId, 0, QStringLiteral("Welcome")).ok());
        QVERIFY(plans::setNotes(db, planId, 1, QStringLiteral("Start the countdown at 9:55")).ok());
        const PlanView original = loadPlan(db, planId).value();

        const QString file = folder.filePath(QStringLiteral("sunday.lumen"));
        const core::Status exported = lumenfile::exportPlan(db, planId, file);
        QVERIFY2(exported.ok(), exported.ok() ? "" : qPrintable(exported.error().message));

        // Into an empty library on another computer.
        auto target = Database::open(Database::inMemory());
        QVERIFY(target.ok());
        auto imported = lumenfile::importFile(*target.value(), file, folder.filePath(QStringLiteral("imported")));
        QVERIFY2(imported.ok(), imported.ok() ? "" : qPrintable(imported.error().message));
        QCOMPARE(imported.value().presentationsReused, 0);
        QCOMPARE(imported.value().mediaAdded, 1 + 3); // the picture and three slide pictures
        const PlanView copy = loadPlan(*target.value(), imported.value().planId).value();
        QCOMPARE(copy.name, original.name);
        QCOMPARE(titles(copy), titles(original));
        QCOMPARE(int(copy.items.size()), int(original.items.size()));
        for (size_t i = 0; i < copy.items.size(); ++i) {
            QCOMPARE(copy.items[i].kind, original.items[i].kind);
            QCOMPARE(copy.items[i].notes, original.items[i].notes);
            QCOMPARE(int(copy.items[i].slides.size()), int(original.items[i].slides.size()));
            for (size_t s = 0; s < copy.items[i].slides.size(); ++s) {
                QCOMPARE(copy.items[i].slides[s].text, original.items[i].slides[s].text);
                QCOMPARE(copy.items[i].slides[s].group, original.items[i].slides[s].group);
                if (!original.items[i].slides[s].mediaPath.isEmpty()) {
                    // The media came along, byte for byte, in the new library's folder.
                    const QString path = copy.items[i].slides[s].mediaPath;
                    QVERIFY(path.startsWith(folder.filePath(QStringLiteral("imported"))));
                    QCOMPARE(MediaLibrary::fingerprint(path), MediaLibrary::fingerprint(original.items[i].slides[s].mediaPath));
                }
            }
        }

        // Importing next week's plan again: nothing is duplicated.
        auto again = lumenfile::importFile(*target.value(), file, folder.filePath(QStringLiteral("imported")));
        QVERIFY(again.ok());
        QCOMPARE(again.value().presentationsAdded, 0);
        QCOMPARE(again.value().mediaAdded, 0);
        QCOMPARE(again.value().presentationsReused, imported.value().presentationsAdded);

        // Not a .lumen file.
        QFile junk(folder.filePath(QStringLiteral("junk.lumen")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("junk");
        junk.close();
        QVERIFY(!lumenfile::importFile(*target.value(), junk.fileName(), folder.path()).ok());
    }

    // The plan menu and item menu in the window.
    void planMenu()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        auto *plan = qobject_cast<RowModel *>(ui.controller->plan());
        const int before = plan->rowCount();
        ui.controller->selectItem(0);

        // Add a header after the first item.
        QVERIFY(ui.click(QStringLiteral("planMenuButton")));
        QVERIFY(ui.item(QStringLiteral("planMenu"))->isVisible());
        QVERIFY(ui.click(QStringLiteral("planAddHeader")));
        ui.settle();
        for (const char c : QByteArrayLiteral("Worship"))
            QTest::keyClick(ui.window, c);
        QTest::keyClick(ui.window, Qt::Key_Return);
        QCOMPARE(plan->rowCount(), before + 1);
        QCOMPARE(plan->get(1).value("title").toString(), QStringLiteral("Worship"));
        QVERIFY(plan->get(1).value("header").toBool());
        QVERIFY(!ui.item(QStringLiteral("planMenu"))->isVisible());

        // Right-click the header: move it down, then remove it.
        ui.settle();
        QQuickItem *row = ui.item(QStringLiteral("planItem-1"));
        QVERIFY(row);
        QTest::mouseClick(ui.window, Qt::RightButton, {},
                          row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint());
        QVERIFY(ui.item(QStringLiteral("itemMenu"))->isVisible());
        QVERIFY(ui.click(QStringLiteral("itemMoveDown")));
        QCOMPARE(plan->get(2).value("title").toString(), QStringLiteral("Worship"));
        ui.settle();
        row = ui.item(QStringLiteral("planItem-2"));
        QTest::mouseClick(ui.window, Qt::RightButton, {},
                          row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint());
        QVERIFY(ui.click(QStringLiteral("itemRemove")));
        QCOMPARE(plan->rowCount(), before);

        // Duplicate, then open the original again from the list.
        const QString original = ui.controller->planName();
        QVERIFY(ui.click(QStringLiteral("planMenuButton")));
        QVERIFY(ui.click(QStringLiteral("planDuplicate")));
        QCOMPARE(ui.controller->planName(), original + QStringLiteral(" (copy)"));
        QCOMPARE(plan->rowCount(), before);
        QVERIFY(ui.click(QStringLiteral("planMenuButton")));
        QVERIFY(ui.click(QStringLiteral("planOpen")));
        auto *saved = qobject_cast<RowModel *>(ui.controller->savedPlans());
        QCOMPARE(saved->rowCount(), 2);
        int originalRow = -1;
        for (int i = 0; i < saved->rowCount(); ++i)
            originalRow = saved->get(i).value("name").toString() == original ? i : originalRow;
        QVERIFY(originalRow >= 0);
        QVERIFY(ui.click(QStringLiteral("savedPlan-%1").arg(originalRow)));
        QCOMPARE(ui.controller->planName(), original);
    }

    // Batch 18: the Share popover, against this same computer.
    void sharePopover()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QVERIFY(ui.click("planMenuButton"));
        QVERIFY(ui.click("planShare"));
        ui.settle();
        QVERIFY(ui.item("sharePopover") && ui.item("shareToggle"));
        QVERIFY(ui.click("shareToggle"));
        QVERIFY(ui.sharing->enabled());
        QTRY_COMPARE(ui.item("shareCode")->property("text").toString(), ui.sharing->code());
        QVERIFY(ui.click("shareAddress"));
        for (const QChar c : QStringLiteral("127.0.0.1:%1").arg(ui.sharing->port()))
            QTest::keyClick(ui.window, c.toLatin1());
        QVERIFY(ui.click("shareRemoteCode"));
        for (const QChar c : ui.sharing->code())
            QTest::keyClick(ui.window, c.toLatin1());
        QVERIFY(ui.click("shareConnect"));
        QTRY_VERIFY(ui.sharing->remotePlans()->property("count").toInt() >= 1);
        const int before = int(Repository<Plan>(ui.database()).list().value().size());
        ui.settle();
        QVERIFY(ui.click("shareCopyPlan-0"));
        QTRY_COMPARE(int(Repository<Plan>(ui.database()).list().value().size()), before + 1);

        // Two operators, one screen: not offered until the screen allows it.
        QVERIFY(!ui.item("screenLinkToggle")->isVisible());
        QVERIFY(ui.click("shareTakesScreen"));
        QVERIFY(ui.sharing->takesScreen());
        QVERIFY(ui.click("shareConnect")); // again, to learn it takes a screen
        QTRY_VERIFY(ui.sharing->remoteTakesScreen());
        ui.settle();
        QTRY_VERIFY(ui.item("screenLinkToggle")->isVisible());
        QVERIFY(ui.click("screenLinkToggle"));
        QTRY_VERIFY(ui.screenLink->active());
        QTRY_VERIFY(ui.screenLink->status().startsWith(QStringLiteral("Live goes to Test PC")));
        QTRY_VERIFY(ui.item("liveRemoteScreen")->isVisible());
        QVERIFY(ui.click("screenLinkToggle")); // Stop sending
        QVERIFY(!ui.screenLink->active());
        QTRY_VERIFY(!ui.item("liveRemoteScreen")->isVisible());
        QVERIFY(ui.click("shareTakesScreen"));
        QVERIFY(!ui.sharing->takesScreen());
        QVERIFY(ui.click("shareToggle"));
        QVERIFY(!ui.sharing->enabled());
    }
};

int main(int argc, char *argv[])
{
    if (argc == 3 && qstrcmp(argv[1], "--crash-child") == 0) {
        QGuiApplication app(argc, argv);
        return crashChild(QString::fromLocal8Bit(argv[2]));
    }
    QGuiApplication app(argc, argv);
    TestPlans test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_plans.moc"
