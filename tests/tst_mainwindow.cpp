#include "operatorcontroller.h"
#include "sampleservice.h"
#include "servicedata.h"

#include "lumen/core/data/database.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Lumen_UiPlugin)
Q_IMPORT_QML_PLUGIN(Lumen_SlidesPlugin)

using namespace lumen;

// Loads the real main window offscreen and checks the five regions from the
// design (toolbar, service plan, slide grid, preview/live, library), with an
// empty library and with the mockup's sample service (test data).
//
// Batch 5: the window now takes an OperatorController instead of reading the
// QML SampleData singleton, so this test builds one; the checks are the same.
//
// Set LUMEN_SCREENSHOT_DIR to save a PNG of each mode for comparing with
// design/mockups/Main.html.
class TestMainWindow : public QObject
{
    Q_OBJECT

private:
    void load(bool sampleService)
    {
        auto db = core::data::Database::open(core::data::Database::inMemory());
        QVERIFY(db.ok());
        m_db = std::move(db).value();
        if (sampleService)
            QVERIFY(app::seedSampleService(*m_db).ok());

        m_show = std::make_unique<core::show::ShowController>();
        m_controller = std::make_unique<app::OperatorController>(*m_show, false);
        auto plan = app::loadLatestPlan(*m_db);
        QVERIFY(plan.ok());
        m_controller->setPlan(plan.value());
        auto songs = app::loadSongs(*m_db);
        QVERIFY(songs.ok());
        m_controller->setSongs(songs.value());

        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->setInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue<QObject *>(m_controller.get())}});
        m_engine->loadFromModule("Lumen.Ui", "Main");
        QCOMPARE(m_engine->rootObjects().size(), 1);
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
        QVERIFY(m_window);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
    }

    // Searches the visual item tree, which also reaches Repeater delegates.
    static QQuickItem *findItem(QQuickItem *item, const QString &name)
    {
        if (!item)
            return nullptr;
        if (item->objectName() == name)
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *found = findItem(child, name))
                return found;
        }
        return nullptr;
    }

    QQuickItem *region(const char *name) const
    {
        return findItem(m_window->contentItem(), QLatin1String(name));
    }

    static int count(QObject *model) { return model->property("count").toInt(); }

    static void addModes()
    {
        QTest::addColumn<bool>("sampleService");
        QTest::newRow("default") << false;
        QTest::newRow("sample service") << true;
    }

    std::unique_ptr<core::data::Database> m_db;
    std::unique_ptr<core::show::ShowController> m_show;
    std::unique_ptr<app::OperatorController> m_controller;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;

private slots:
    void init()
    {
        // Any QML warning (bad binding, binding loop, missing type) fails the test.
        // Any warning fails, except Qt's note that a missing font family
        // made font lookup slow (macOS, where "Sans Serif" does not exist).
        QTest::failOnWarning(QRegularExpression(QStringLiteral("^(?!Populating font family aliases took ).*")));
    }

    void cleanup()
    {
        m_engine.reset();
        m_window = nullptr;
        m_controller.reset();
        m_show.reset();
        m_db.reset();
    }

    void windowHasTitle_data() { addModes(); }
    void windowHasTitle()
    {
        QFETCH(bool, sampleService);
        load(sampleService);
        QCOMPARE(m_window->title(), QStringLiteral("Lumen Presenter"));
    }

    void hasFiveRegions_data()
    {
        QTest::addColumn<bool>("sampleService");
        QTest::addColumn<QString>("name");
        for (bool preview : {false, true}) {
            const char *mode = preview ? "preview" : "default";
            for (const char *name : {"toolbar", "servicePlan", "slideGrid", "previewLive", "library"})
                QTest::addRow("%s/%s", mode, name) << preview << QString::fromLatin1(name);
        }
    }

    void hasFiveRegions()
    {
        QFETCH(bool, sampleService);
        QFETCH(QString, name);
        load(sampleService);
        auto *item = m_window->findChild<QQuickItem *>(name);
        QVERIFY2(item, qPrintable(name));
        QVERIFY(item->isVisible());
        QTRY_VERIFY(item->width() > 0 && item->height() > 0);
    }

    void regionsAreLaidOutAsDesigned_data() { addModes(); }
    void regionsAreLaidOutAsDesigned()
    {
        QFETCH(bool, sampleService);
        load(sampleService);
        auto *root = m_window->contentItem();
        auto *toolbar = region("toolbar");
        auto *plan = region("servicePlan");
        auto *grid = region("slideGrid");
        auto *preview = region("previewLive");
        auto *library = region("library");
        QVERIFY(toolbar && plan && grid && preview && library);

        auto pos = [root](QQuickItem *i) { return i->mapToItem(root, QPointF(0, 0)); };
        QTRY_VERIFY(grid->width() > 0);

        // Toolbar on top, library at the bottom, the three columns between.
        QVERIFY(pos(toolbar).y() < pos(plan).y());
        QVERIFY(pos(plan).y() < pos(library).y());
        // Service plan left, slide grid centre, preview and live right.
        QVERIFY(pos(plan).x() < pos(grid).x());
        QVERIFY(pos(grid).x() < pos(preview).x());
        // The slide grid takes the spare width.
        QVERIFY(grid->width() > plan->width());
        QVERIFY(grid->width() > preview->width());
        // Regions fill the window with no gaps.
        QCOMPARE(pos(library).y() + library->height(), root->height());
        QCOMPARE(pos(preview).x() + preview->width(), root->width());
    }

    // The normal app must never show made-up service content.
    void defaultModeShowsEmptyStates()
    {
        load(false);
        QCOMPARE(count(m_controller->plan()), 0);
        QCOMPARE(count(m_controller->slides()), 0);
        QCOMPARE(count(m_controller->songs()), 0);
        QCOMPARE(m_controller->liveText(), QString());
        QCOMPARE(m_controller->planName(), QStringLiteral("No service plan"));
        QVERIFY(!m_controller->outputConnected());
    }

    // With the sample service (test data), the window reproduces the mockup.
    void sampleServiceShowsMockupContent()
    {
        load(true);
        QCOMPARE(m_controller->planName(), QStringLiteral("Sunday Service"));
        QCOMPARE(count(m_controller->plan()), 9);
        m_controller->selectItem(2);
        QCOMPARE(m_controller->itemTitle(), QStringLiteral("Amazing Grace"));
        QCOMPARE(m_controller->itemDetail(), QStringLiteral("John Newton · Public domain · Theme: Lyrics, centred"));
        QCOMPARE(count(m_controller->slides()), 9);
        QCOMPARE(m_controller->arrangement().size(), 5);
        QVERIFY(m_controller->goLive(2));
        QCOMPARE(m_controller->liveSlide(), 2);
        QCOMPARE(m_controller->previewSlide(), 3);
        QVERIFY(m_controller->liveText().startsWith(QLatin1String("'Twas grace")));
        QCOMPARE(m_controller->liveLabel(), QStringLiteral("Verse 2 · slide 3"));
        QCOMPARE(m_controller->layers().size(), 5);
        QCOMPARE(count(m_controller->songs()), 4);
        // The cards in the window show the same states.
        QTRY_VERIFY(region("slide-2"));
        QCOMPARE(region("slide-2")->property("status").toString(), QStringLiteral("live"));
        QCOMPARE(region("slide-3")->property("status").toString(), QStringLiteral("next"));
    }

    void editModeIsHiddenUnlessEnabled()
    {
        load(true);
        auto *editButton = region("editMode");
        QVERIFY(editButton);
        QVERIFY(!editButton->isVisible());
        m_controller->setMode(QStringLiteral("edit"));
        QCOMPARE(m_controller->mode(), QStringLiteral("show"));
    }

    // Inter is compiled into the UI module (not taken from the system), loads,
    // and is the font the window's text uses.
    void interFontIsBundledAndUsed()
    {
        for (const char *weight : {"Regular", "Medium", "SemiBold", "Bold"}) {
            const QString path = QStringLiteral(":/qt/qml/Lumen/Ui/fonts/Inter-%1.ttf")
                                     .arg(QLatin1String(weight));
            QVERIFY2(QFile::exists(path), qPrintable(path));
            const int id = QFontDatabase::addApplicationFont(path);
            QVERIFY2(id != -1, qPrintable(path));
            QVERIFY(QFontDatabase::applicationFontFamilies(id).contains(QStringLiteral("Inter")));
        }
        QVERIFY(QFile::exists(QStringLiteral(":/qt/qml/Lumen/Ui/fonts/OFL.txt")));

        load(false);
        // Batch 8: slide pictures inside the Preview and Live frames are
        // audience-output text, drawn with the output fonts; skip them.
        auto inSlide = [](QQuickItem *item) {
            for (QQuickItem *p = item; p; p = p->parentItem()) {
                if (p->objectName() == QLatin1String("slideContent"))
                    return true;
            }
            return false;
        };
        int textItems = 0;
        const auto items = m_window->findChildren<QQuickItem *>();
        for (QQuickItem *item : items) {
            if (!item->inherits("QQuickText") || inSlide(item))
                continue;
            ++textItems;
            const QFont font = item->property("font").value<QFont>();
            QCOMPARE(font.family(), QStringLiteral("Inter"));
            QCOMPARE(QFontInfo(font).family(), QStringLiteral("Inter"));
        }
        QVERIFY(textItems > 10);
    }

    void screenshots_data() { addModes(); }
    void screenshots()
    {
        const QString dir = qEnvironmentVariable("LUMEN_SCREENSHOT_DIR");
        if (dir.isEmpty())
            QSKIP("LUMEN_SCREENSHOT_DIR not set");
        QFETCH(bool, sampleService);
        load(sampleService);
        if (sampleService) {
            // The mockup's moment: Amazing Grace, verse 2 live.
            m_controller->selectItem(2);
            m_controller->goLive(2);
        }
        QTest::qWait(150);
        const QImage image = m_window->grabWindow();
        QVERIFY(!image.isNull());
        const QString file = QDir(dir).filePath(sampleService ? QStringLiteral("main-preview.png")
                                                              : QStringLiteral("main-default.png"));
        QVERIFY(image.save(file));
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
