#include <QDir>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Lumen_UiPlugin)

// Loads the real main window offscreen and checks the five regions from the
// design (toolbar, service plan, slide grid, preview/live, library), in both
// the default mode and the --design-preview mode.
//
// Set LUMEN_SCREENSHOT_DIR to save a PNG of each mode for comparing with
// design/mockups/Main.html.
class TestMainWindow : public QObject
{
    Q_OBJECT

private:
    void load(bool designPreview)
    {
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->setInitialProperties({{QStringLiteral("designPreview"), designPreview}});
        m_engine->loadFromModule("Lumen.Ui", "Main");
        QCOMPARE(m_engine->rootObjects().size(), 1);
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
        QVERIFY(m_window);
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
    }

    QQuickItem *region(const char *name) const
    {
        return m_window->findChild<QQuickItem *>(QLatin1String(name));
    }

    static void addModes()
    {
        QTest::addColumn<bool>("designPreview");
        QTest::newRow("default") << false;
        QTest::newRow("design preview") << true;
    }

private slots:
    void init()
    {
        // Any QML warning (bad binding, binding loop, missing type) fails the test.
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    }

    void cleanup()
    {
        m_engine.reset();
        m_window = nullptr;
    }

    void windowHasTitle_data() { addModes(); }
    void windowHasTitle()
    {
        QFETCH(bool, designPreview);
        load(designPreview);
        QCOMPARE(m_window->title(), QStringLiteral("Lumen Presenter"));
    }

    void hasFiveRegions_data()
    {
        QTest::addColumn<bool>("designPreview");
        QTest::addColumn<QString>("name");
        for (bool preview : {false, true}) {
            const char *mode = preview ? "preview" : "default";
            for (const char *name : {"toolbar", "servicePlan", "slideGrid", "previewLive", "library"})
                QTest::addRow("%s/%s", mode, name) << preview << QString::fromLatin1(name);
        }
    }

    void hasFiveRegions()
    {
        QFETCH(bool, designPreview);
        QFETCH(QString, name);
        load(designPreview);
        auto *item = m_window->findChild<QQuickItem *>(name);
        QVERIFY2(item, qPrintable(name));
        QVERIFY(item->isVisible());
        QTRY_VERIFY(item->width() > 0 && item->height() > 0);
    }

    void regionsAreLaidOutAsDesigned_data() { addModes(); }
    void regionsAreLaidOutAsDesigned()
    {
        QFETCH(bool, designPreview);
        load(designPreview);
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
        QCOMPARE(region("servicePlan")->property("items").toList().size(), 0);
        QCOMPARE(region("slideGrid")->property("slides").toList().size(), 0);
        QCOMPARE(region("library")->property("songs").toList().size(), 0);
        QCOMPARE(region("previewLive")->property("liveText").toString(), QString());
        QCOMPARE(region("toolbar")->property("outputsConnected").toBool(), false);
    }

    // --design-preview reproduces the mockup's content.
    void designPreviewShowsMockupContent()
    {
        load(true);
        QCOMPARE(region("servicePlan")->property("items").toList().size(), 9);
        QCOMPARE(region("servicePlan")->property("currentIndex").toInt(), 2);
        QCOMPARE(region("slideGrid")->property("title").toString(), QStringLiteral("Amazing Grace"));
        QCOMPARE(region("slideGrid")->property("slides").toList().size(), 9);
        QCOMPARE(region("slideGrid")->property("liveIndex").toInt(), 2);
        QCOMPARE(region("slideGrid")->property("nextIndex").toInt(), 3);
        QVERIFY(region("previewLive")->property("liveText").toString().startsWith("'Twas grace"));
        QCOMPARE(region("previewLive")->property("layers").toList().size(), 5);
        QCOMPARE(region("library")->property("songs").toList().size(), 4);
    }

    void screenshots_data() { addModes(); }
    void screenshots()
    {
        const QString dir = qEnvironmentVariable("LUMEN_SCREENSHOT_DIR");
        if (dir.isEmpty())
            QSKIP("LUMEN_SCREENSHOT_DIR not set");
        QFETCH(bool, designPreview);
        load(designPreview);
        QTest::qWait(100);
        const QImage image = m_window->grabWindow();
        QVERIFY(!image.isNull());
        const QString file = QDir(dir).filePath(designPreview ? QStringLiteral("main-preview.png")
                                                              : QStringLiteral("main-default.png"));
        QVERIFY(image.save(file));
    }

private:
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
