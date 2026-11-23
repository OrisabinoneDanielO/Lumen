#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Lumen_UiPlugin)

// Loads the real main window offscreen and checks the five regions from the
// design (toolbar, service plan, slide grid, preview/live, library) exist.
class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Any QML warning (bad binding, missing type) fails the test.
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_engine->loadFromModule("Lumen.Ui", "Main");
        QCOMPARE(m_engine->rootObjects().size(), 1);
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
        QVERIFY(m_window);
    }

    void cleanup()
    {
        m_engine.reset();
        m_window = nullptr;
    }

    void windowHasTitleAndShows()
    {
        QCOMPARE(m_window->title(), QStringLiteral("Lumen Presenter"));
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
    }

    void hasFiveRegions_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("toolbar") << QStringLiteral("toolbar");
        QTest::newRow("service plan") << QStringLiteral("servicePlan");
        QTest::newRow("slide grid") << QStringLiteral("slideGrid");
        QTest::newRow("preview and live") << QStringLiteral("previewLive");
        QTest::newRow("library") << QStringLiteral("library");
    }

    void hasFiveRegions()
    {
        QFETCH(QString, name);
        auto *region = m_window->findChild<QQuickItem *>(name);
        QVERIFY2(region, qPrintable(name));
        QVERIFY(region->isVisible());
        QTRY_VERIFY(region->width() > 0 && region->height() > 0);
    }

    void regionsAreLaidOutAsDesigned()
    {
        auto *root = m_window->contentItem();
        auto find = [this](const char *n) { return m_window->findChild<QQuickItem *>(n); };
        auto *toolbar = find("toolbar");
        auto *plan = find("servicePlan");
        auto *grid = find("slideGrid");
        auto *preview = find("previewLive");
        auto *library = find("library");
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
    }

private:
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
