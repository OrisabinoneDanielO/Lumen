#include "operatorharness.h"
#include "screensetup.h"

#include "lumen/core/render/renderlink.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QProcess>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QWindow>

using namespace lumen;
using lumen::app::ScreenSetup;
using lumen::core::render::RenderLink;

// The screen setup: three virtual screens (the operator's laptop, a
// projector and a TV for the stage), set up in the main.
namespace {

const char *const screensFile = "lumen-three-screens.json";

void writeScreens()
{
    QFile config(QString::fromLatin1(screensFile));
    if (!config.open(QIODevice::WriteOnly | QIODevice::Truncate))
        qFatal("cannot write %s", screensFile);
    config.write(R"({"screens": [
        {"name": "Laptop", "x": 0, "y": 0, "width": 1440, "height": 900, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1},
        {"name": "Projector", "x": 1440, "y": 0, "width": 1920, "height": 1080, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1},
        {"name": "Stage TV", "x": 3360, "y": 0, "width": 1280, "height": 720, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1}
    ]})");
}

void forgetSetups()
{
    QSettings().remove(QStringLiteral("screens/setups"));
}

} // namespace

class TestScreens : public QObject
{
    Q_OBJECT

    std::unique_ptr<RenderLink> startRenderer(const QStringList &arguments)
    {
        RenderLink::Options options;
        options.serverName = QStringLiteral("lumen-screens-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        options.executable = QStringLiteral(LUMEN_RENDER_PATH);
        options.arguments = arguments + QStringList{QStringLiteral("--test-mode"),
                                                    QStringLiteral("--watch-pid=%1").arg(QCoreApplication::applicationPid())};
        auto link = std::make_unique<RenderLink>(options);
        auto started = link->start();
        if (!started.ok()) {
            qWarning("%s", qPrintable(started.error().message));
            return nullptr;
        }
        return link;
    }

    static QJsonObject status(RenderLink &link)
    {
        auto reply = link.request({{"cmd", "status"}});
        return reply.ok() ? reply.value() : QJsonObject();
    }

private slots:
    void init() { forgetSetups(); }

    void threeScreens()
    {
        QCOMPARE(QGuiApplication::screens().size(), 3);
        QCOMPARE(QGuiApplication::primaryScreen()->name(), QStringLiteral("Laptop"));
    }

    // A new set of screens: the operator keeps the primary, the output goes to
    // the first other screen, and the setup is needed.
    void defaultsForNewScreens()
    {
        ScreenSetup setup;
        QCOMPARE(setup.count(), 3);
        QCOMPARE(setup.role(0), QStringLiteral("operator"));
        QCOMPARE(setup.role(1), QStringLiteral("main"));
        QCOMPARE(setup.role(2), QStringLiteral("none"));
        QCOMPARE(setup.mainScreen(), 1);
        QCOMPARE(setup.stageScreen(), -1);
        QVERIFY(setup.needsSetup());

        const QJsonObject label = setup.label(1);
        QCOMPARE(label.value("number").toInt(), 2);
        QCOMPARE(label.value("name").toString(), QStringLiteral("Projector"));
        QCOMPARE(label.value("role").toString(), QStringLiteral("Main output"));
    }

    // A role has one screen and a screen one role; the operator's screen
    // keeps the operator window.
    void rolesAreExclusive()
    {
        ScreenSetup setup;
        QSignalSpy moved(&setup, &ScreenSetup::rolesChanged);
        setup.setRole(2, QStringLiteral("main"));
        QCOMPARE(setup.role(1), QStringLiteral("none"));
        QCOMPARE(setup.mainScreen(), 2);
        setup.setRole(1, QStringLiteral("stage"));
        QCOMPARE(setup.stageScreen(), 1);
        setup.setRole(1, QStringLiteral("main"));
        QCOMPARE(setup.mainScreen(), 1);
        QCOMPARE(setup.role(2), QStringLiteral("none"));
        QCOMPARE(setup.stageScreen(), -1);
        QCOMPARE(moved.count(), 3);

        setup.setRole(0, QStringLiteral("main"));
        setup.setRole(2, QStringLiteral("projector")); // not a role
        QCOMPARE(setup.role(0), QStringLiteral("operator"));
        QCOMPARE(setup.role(2), QStringLiteral("none"));
        QCOMPARE(moved.count(), 3);
    }

    // Closing the setup remembers it for these screens; the next start
    // restores it without asking.
    void rememberedForTheseScreens()
    {
        {
            ScreenSetup setup;
            setup.openSetup();
            setup.setRole(2, QStringLiteral("main"));
            setup.setRole(1, QStringLiteral("stage"));
            setup.close();
            QVERIFY(!setup.needsSetup());
        }
        ScreenSetup next;
        QVERIFY(!next.needsSetup());
        QCOMPARE(next.mainScreen(), 2);
        QCOMPARE(next.stageScreen(), 1);
    }

    // While open, the operator window draws the labels of the screens no
    // renderer is on.
    void labelsWhereNoRendererIs()
    {
        ScreenSetup setup;
        auto *rows = qobject_cast<app::RowModel *>(setup.screens());
        QVERIFY(rows);
        QVERIFY(!rows->get(1).value("labelHere").toBool()); // closed: no labels
        setup.openSetup();
        QVERIFY(!rows->get(0).value("labelHere").toBool()); // the operator's screen
        QVERIFY(rows->get(1).value("labelHere").toBool());
        QVERIFY(rows->get(2).value("labelHere").toBool());
        setup.setRunning(true, false); // the main output draws its own
        QVERIFY(!rows->get(1).value("labelHere").toBool());
        QVERIFY(rows->get(2).value("labelHere").toBool());
        // The arrangement: side by side, left to right.
        QVERIFY(rows->get(0).value("areaX").toDouble() < rows->get(1).value("areaX").toDouble());
        QVERIFY(rows->get(1).value("areaX").toDouble() < rows->get(2).value("areaX").toDouble());
    }

    // The Screens panel in the operator window: opens with the setup, its
    // buttons give the roles, Done closes it; Plan ▾ → Screens… opens it.
    void panelInTheWindow()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        QVERIFY(!ui.item("screensPopover")->property("active").toBool());

        ui.screens->openSetup();
        QTRY_VERIFY(ui.item("screensPopover")->property("active").toBool());
        QTRY_VERIFY(ui.item("screenRow-3"));
        QVERIFY(ui.item("screenTile-2"));
        QVERIFY(ui.click("screenRole-3-stage"));
        QCOMPARE(ui.screens->stageScreen(), 2);
        QVERIFY(ui.click("screenRole-3-main"));
        QCOMPARE(ui.screens->mainScreen(), 2);
        QCOMPARE(ui.screens->role(1), QStringLiteral("none"));

        // The labels on the screens without a renderer (none runs here).
        const auto labels = [] {
            QStringList shown;
            for (QWindow *w : QGuiApplication::allWindows()) {
                if (w->objectName().startsWith(QLatin1String("screenLabel-")) && w->isVisible())
                    shown << QStringLiteral("%1@%2").arg(w->objectName(), w->screen()->name());
            }
            shown.sort();
            return shown;
        };
        QTRY_COMPARE(labels(), (QStringList{QStringLiteral("screenLabel-2@Projector"), QStringLiteral("screenLabel-3@Stage TV")}));

        // LUMEN_SCREENS_DUMP=folder: pictures of the panel and of a label.
        if (const QString dir = qEnvironmentVariable("LUMEN_SCREENS_DUMP"); !dir.isEmpty()) {
            QTest::qWait(200);
            QVERIFY(ui.window->grabWindow().save(QDir(dir).filePath(QStringLiteral("screens-panel.png"))));
            for (QWindow *w : QGuiApplication::allWindows()) {
                if (w->objectName() == QLatin1String("screenLabel-3"))
                    QVERIFY(qobject_cast<QQuickWindow *>(w)->grabWindow().save(QDir(dir).filePath(QStringLiteral("screens-label.png"))));
            }
        }

        // While the setup is open, the live shortcuts do nothing.
        ui.controller->selectItem(2);
        const QString live = ui.controller->property("liveText").toString();
        const int liveSlide = ui.controller->property("liveSlide").toInt();
        QTest::keyClick(ui.window, Qt::Key_Space);
        QTest::keyClick(ui.window, Qt::Key_F7);
        QCOMPARE(ui.controller->property("liveText").toString(), live);
        QCOMPARE(ui.controller->property("liveSlide").toInt(), liveSlide);
        QVERIFY(!ui.controller->property("blackOn").toBool());
        QVERIFY(ui.screens->isOpen());

        QVERIFY(ui.click("screensDone"));
        QTRY_VERIFY(!ui.item("screensPopover")->property("active").toBool());
        QVERIFY(!ui.screens->isOpen());
        QTRY_VERIFY(labels().isEmpty());

        QVERIFY(ui.click("planMenuButton"));
        QTRY_VERIFY(ui.item("planScreens"));
        QVERIFY(ui.click("planScreens"));
        QTRY_VERIFY(ui.screens->isOpen());
        // Escape closes it.
        QTRY_VERIFY(ui.window->activeFocusItem());
        QTest::keyClick(ui.window, Qt::Key_Escape);
        QTRY_VERIFY(!ui.screens->isOpen());
    }

    // The renderer moves its window to another screen and shows the label
    // above its picture.
    void rendererMovesAndShowsItsLabel()
    {
        auto link = startRenderer({});
        QVERIFY(link);
        QCOMPARE(status(*link).value("screen").toString(), QStringLiteral("Projector"));

        QVERIFY(link->request({{"cmd", "screen"}, {"index", 2}}).ok());
        QTRY_COMPARE(status(*link).value("screen").toString(), QStringLiteral("Stage TV"));
        QVERIFY(!link->request({{"cmd", "screen"}, {"index", 7}}).ok());

        const QJsonObject label{{"number", 3}, {"name", "Stage TV"}, {"role", "Main output"}};
        QVERIFY(link->request({{"cmd", "identify"}, {"identify", label}}).ok());
        QCOMPARE(status(*link).value("identify").toObject(), label);
        QTRY_COMPARE(link->request({{"cmd", "texts"}, {"prefix", "identify"}}).value().value("texts").toObject().value("identifyNumber").toString(),
                     QStringLiteral("3"));
        QVERIFY(link->request({{"cmd", "identify"}, {"identify", QJsonObject()}}).ok());
        QTRY_VERIFY(link->request({{"cmd", "texts"}, {"prefix", "identify"}}).value().value("texts").toObject().isEmpty());

        QVERIFY(link->request({{"cmd", "screen"}, {"windowed", true}}).ok());
        QTRY_COMPARE(status(*link).value("screen").toString(), QStringLiteral("Laptop"));
        (void)link->quit();
    }

    // The app itself: with a remembered setup, the main output and the stage
    // display open on their screens at start. (Linux: the app's settings
    // folder is moved with XDG_CONFIG_HOME; elsewhere it cannot be.)
    void appRestoresTheSetup()
    {
#if !defined(Q_OS_LINUX)
        QSKIP("the app's settings can only be redirected on Linux");
#else
        QTemporaryDir home;
        QVERIFY(home.isValid());
        QStringList keys;
        for (const QScreen *screen : QGuiApplication::screens())
            keys << ScreenSetup::screenKey(screen);
        const QJsonObject roles{{keys.at(0), "operator"}, {keys.at(1), "stage"}, {keys.at(2), "main"}};
        {
            QSettings saved(home.filePath(QStringLiteral("config/Lumen/Lumen Presenter.conf")), QSettings::IniFormat);
            saved.setValue(QStringLiteral("screens/setups"),
                           QJsonDocument(QJsonObject{{ScreenSetup::setKey(keys), roles}}).toJson(QJsonDocument::Compact));
            saved.setValue(QStringLiteral("stage/enabled"), true);
        }

        const QString server = QStringLiteral("lumen-screens-app-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        QProcess lumen;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), home.filePath(QStringLiteral("config")));
        env.insert(QStringLiteral("XDG_DATA_HOME"), home.filePath(QStringLiteral("data")));
        lumen.setProcessEnvironment(env);
        lumen.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        lumen.start(QStringLiteral(LUMEN_APP_PATH),
                    {QStringLiteral("--render-server=%1").arg(server),
                     QStringLiteral("--render-executable=%1").arg(QStringLiteral(LUMEN_RENDER_PATH)),
                     QStringLiteral("--quit-after=15000")});
        QVERIFY(lumen.waitForStarted());

        RenderLink::Options mainOptions;
        mainOptions.serverName = server;
        RenderLink main(mainOptions);
        QTRY_VERIFY_WITH_TIMEOUT(main.connectToRunning(200).ok(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(status(main).value("screen").toString(), QStringLiteral("Stage TV"), 5000);

        RenderLink::Options stageOptions;
        stageOptions.serverName = server + QStringLiteral("-stage");
        RenderLink stage(stageOptions);
        QTRY_VERIFY_WITH_TIMEOUT(stage.connectToRunning(200).ok(), 10000);
        QCOMPARE(status(stage).value("kind").toString(), QStringLiteral("stage"));
        QTRY_COMPARE_WITH_TIMEOUT(status(stage).value("screen").toString(), QStringLiteral("Projector"), 5000);
        // A remembered setup does not open the setup again: no labels.
        QVERIFY(status(main).value("identify").toObject().isEmpty());

        lumen.terminate();
        if (!lumen.waitForFinished(5000))
            lumen.kill();
        (void)main.quit();
        (void)stage.quit();
#endif
    }

    void cleanupTestCase() { forgetSetups(); }
};

int main(int argc, char *argv[])
{
    writeScreens();
    // A relative path: Qt splits platform options at ':' (Windows drive letters).
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen:configfile=") + screensFile);
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Lumen Tests"));
    QGuiApplication::setApplicationName(QStringLiteral("tst_screens"));
    TestScreens test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_screens.moc"
