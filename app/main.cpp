#include "operatorcontroller.h"
#include "scripturecontroller.h"
#include "servicedata.h"
#include "themecontroller.h"
#include "themes.h"
#include "editorcontroller.h"
#include "messagetemplates.h"
#include "propscontroller.h"
#include "stagecontroller.h"
#include "streamcontroller.h"
#include "timercontroller.h"

#include "lumen/core/buildinfo.h"
#include "lumen/core/data/database.h"
#include "lumen/core/render/protocol.h"
#include "lumen/core/render/renderlink.h"
#include "lumen/core/scripture/biblestore.h"
#include "lumen/core/show/outputbridge.h"
#include "lumen/core/show/showcontroller.h"
#include "lumen/core/show/textstyle.h"
#include "lumen/core/songs/songlibrary.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QProcess>
#include <QTimer>
#include <QFile>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Lumen_UiPlugin)
Q_IMPORT_QML_PLUGIN(Lumen_SlidesPlugin)

using namespace lumen;
using namespace lumen::core;

namespace {

constexpr auto outputEnabledKey = "output/enabled"; // on by default since batch 12
constexpr auto stageEnabledKey = "stage/enabled";   // off by default (needs a screen)
constexpr auto stageLayoutKey = "stage/layout";
constexpr auto streamEnabledKey = "stream/enabled";   // off by default
constexpr auto streamKeyFillKey = "stream/keyFill";
constexpr auto streamNdiKey = "stream/ndi";
constexpr auto streamNdiNameKey = "stream/ndiName";
constexpr auto experimentalNdiKey = "experimental/ndi"; // untested: off by default
constexpr auto experimentalEditorKey = "experimental/editor";
constexpr auto linesPerSlideKey = "songs/linesPerSlide"; // auto-split limit, 2 by default
constexpr auto backupFolderKey = "backups/folder"; // default: "backups" beside the library

// The link to the render process and the bridge that feeds it the scene.
struct Output
{
    std::unique_ptr<render::RenderLink> link;
    std::unique_ptr<show::OutputBridge> bridge;
};

// The stage display's renderer (batch 13): on a screen that is neither the
// operator's (the primary) nor the audience output's (the first other one),
// else in a window. Returns its link, or null if it could not start.
std::unique_ptr<render::RenderLink> startStage(const QString &executable, const QString &audienceServer)
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    QScreen *primary = QGuiApplication::primaryScreen();
    QList<int> others;
    for (int i = 0; i < screens.size(); ++i) {
        if (screens.at(i) != primary)
            others << i;
    }
    render::RenderLink::Options options;
    options.serverName = (audienceServer.isEmpty() ? render::defaultServerName() : audienceServer) + QStringLiteral("-stage");
    options.executable = executable;
    options.arguments = {QStringLiteral("--stage"),
                         others.size() >= 2 ? QStringLiteral("--screen=%1").arg(others.at(1)) : QStringLiteral("--windowed")};
    auto link = std::make_unique<render::RenderLink>(options);
    if (auto started = link->start(); !started.ok()) {
        qWarning("Stage display not started: %s", qPrintable(started.error().message));
        return nullptr;
    }
    return link;
}

// Starts the render process (or reconnects to one still showing after a UI
// crash) and feeds it the scene.
std::unique_ptr<Output> startOutput(show::ShowController &controller, const QString &serverName,
                                    const QString &executable, const QStringList &renderOptions)
{
    render::RenderLink::Options options;
    if (!serverName.isEmpty())
        options.serverName = serverName;
    options.executable = executable;
    options.arguments = renderOptions;

    auto output = std::make_unique<Output>();
    output->link = std::make_unique<render::RenderLink>(options);
    if (auto started = output->link->start(); !started.ok()) {
        qWarning("Output not started: %s", qPrintable(started.error().message));
        return nullptr;
    }
    output->bridge = std::make_unique<show::OutputBridge>(controller, *output->link);
    QObject::connect(output->bridge.get(), &show::OutputBridge::sendFailed, [](const QString &error) {
        qWarning("Output: %s", qPrintable(error));
    });

    auto onAir = output->bridge->attach();
    if (!onAir.ok()) {
        qWarning("Output not attached: %s", qPrintable(onAir.error().message));
        return nullptr;
    }
    // A fresh renderer gets a test frame. One that kept running while the UI
    // restarted keeps what it is showing.
    if (onAir.value().sequence == 0) {
        controller.setContent(show::Layer::Background, show::Content::fill(QColor(0x14, 0x21, 0x3D)));
        controller.setContent(show::Layer::Slide, show::Content::textContent(appName()));
    }
    return output;
}

// The library database: the user's own, or the mockup's sample service in
// memory for --design-preview. If the user's cannot be opened the app still
// runs, with an empty in-memory library.
std::unique_ptr<data::Database> openLibrary(bool designPreview)
{
    if (!designPreview) {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(folder);
        const QString path = QDir(folder).filePath(QStringLiteral("library.db"));
        // A restore chosen before the last restart replaces the library now,
        // before it is opened (batch 11).
        if (auto restored = app::Backups::applyPendingRestore(path); !restored.ok())
            qWarning("Backup not restored: %s", qPrintable(restored.error().message));
        else if (restored.value())
            qInfo("Library restored from a backup");
        auto db = data::Database::open(path);
        // A damaged library: keep it aside and start empty rather than fail
        // during a service; the backups are still there to restore.
        if (db.ok()) {
            if (auto intact = db.value()->isIntact(); intact.ok() && !intact.value()) {
                qWarning("The library is damaged; it was kept as library-damaged.db. Restore a backup from the plan menu.");
                db.value().reset();
                QFile::remove(QDir(folder).filePath(QStringLiteral("library-damaged.db")));
                QFile::rename(path, QDir(folder).filePath(QStringLiteral("library-damaged.db")));
                db = data::Database::open(path);
            }
        }
        if (db.ok())
            return std::move(db).value();
        qWarning("Library not opened: %s", qPrintable(db.error().message));
    }
    auto db = data::Database::open(data::Database::inMemory());
    if (!db.ok()) {
        qCritical("No database available: %s", qPrintable(db.error().message));
        return nullptr;
    }
    if (designPreview) {
        if (auto seeded = app::seedSampleService(*db.value()); !seeded.ok())
            qWarning("Sample service not loaded: %s", qPrintable(seeded.error().message));
    }
    return std::move(db).value();
}

// The Bibles built into the app: bibles.db beside the executable, in the
// install's share folder, or in a macOS bundle's Resources.
QString bundledBibles()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    for (const QString &candidate : {QStringLiteral("bibles.db"), QStringLiteral("../share/lumen/bibles.db"),
                                     QStringLiteral("../Resources/bibles.db"), QStringLiteral("../../../bibles.db")}) {
        if (QFileInfo::exists(appDir.filePath(candidate)))
            return appDir.filePath(candidate);
    }
    return {};
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(appName());
    QGuiApplication::setApplicationDisplayName(appName());
    QGuiApplication::setOrganizationName(organisationName());
    QGuiApplication::setApplicationVersion(version());
    show::registerOutputFonts(); // to fit text exactly as the output draws it

    QCommandLineParser parser;
    parser.setApplicationDescription(appName());
    parser.addHelpOption();
    parser.addVersionOption();
    // Off by default: loads the mockup's sample service into a temporary
    // in-memory library, for comparing the window with design/mockups.
    const QCommandLineOption designPreview(
        QStringLiteral("design-preview"),
        QStringLiteral("Show the mockup's sample service (a temporary library, not yours)."));
    // The output starts with the app (setting output/enabled, on by
    // default; the design preview leaves it off unless --output is given).
    // --experimental-output is the name it had before batch 12.
    const QCommandLineOption outputOption(
        QStringList{QStringLiteral("output"), QStringLiteral("experimental-output")},
        QStringLiteral("Start the audience output (the default, except with --design-preview)."));
    const QCommandLineOption noOutputOption(QStringLiteral("no-output"),
                                            QStringLiteral("Do not start the audience output."));
    const QCommandLineOption renderServer(QStringLiteral("render-server"),
                                          QStringLiteral("Local socket name of the renderer."),
                                          QStringLiteral("name"));
    const QCommandLineOption renderExecutable(QStringLiteral("render-executable"),
                                              QStringLiteral("Path to lumen-render."),
                                              QStringLiteral("path"));
    const QCommandLineOption renderOption(QStringLiteral("render-option"),
                                          QStringLiteral("Extra argument for lumen-render (repeatable)."),
                                          QStringLiteral("argument"));
    // For automated checks (the offline test): quit normally after a while.
    const QCommandLineOption quitAfter(QStringLiteral("quit-after"),
                                       QStringLiteral("Quit after <ms> milliseconds (for tests)."),
                                       QStringLiteral("ms"));
    parser.addOptions({designPreview, outputOption, noOutputOption, renderServer, renderExecutable, renderOption, quitAfter});
    parser.process(app);

    const QSettings settings;
    show::ShowController show;
    auto library = openLibrary(parser.isSet(designPreview));
    if (!library)
        return EXIT_FAILURE;

    if (auto themed = app::ensureDefaultThemes(*library); !themed.ok())
        qWarning("Default themes not set up: %s", qPrintable(themed.error().message));

    app::OperatorController controller(show, settings.value(QLatin1String(experimentalEditorKey), false).toBool());
    songs::SongLibrary songLibrary(*library, settings.value(QLatin1String(linesPerSlideKey), 2).toInt());
    if (auto indexed = songLibrary.indexMissing(); !indexed.ok())
        qWarning("Song search index not updated: %s", qPrintable(indexed.error().message));
    controller.attachLibrary(*library, songLibrary);

    // Backups (daily, the newest 14) and the session saved every 30 s. The
    // design preview's library is temporary, so it has neither.
    const QString dataFolder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    std::unique_ptr<app::Backups> backups;
    QTimer backupTimer;
    if (!parser.isSet(designPreview) && library->path() != data::Database::inMemory()) {
        backups = std::make_unique<app::Backups>(
            *library, settings.value(QLatin1String(backupFolderKey), QDir(dataFolder).filePath(QStringLiteral("backups"))).toString());
        controller.attachBackups(*backups, library->path(), QDir(dataFolder).filePath(QStringLiteral("imported")));
        const auto backUpIfDue = [&backups, &controller] {
            if (auto made = backups->backUpIfDue(); !made.ok())
                qWarning("Scheduled backup failed: %s", qPrintable(made.error().message));
            else if (made.value())
                controller.refreshBackups();
        };
        backUpIfDue();
        backupTimer.setInterval(3600 * 1000); // checks hourly; backs up daily
        QObject::connect(&backupTimer, &QTimer::timeout, &app, backUpIfDue);
        backupTimer.start();
    }
    if (!controller.restoreSession()) {
        if (auto plan = app::loadLatestPlan(*library); plan.ok())
            controller.setPlan(plan.value());
        else
            qWarning("Plan not loaded: %s", qPrintable(plan.error().message));
        controller.refreshSavedPlans();
    }
    // Restoring a backup restarts the app, which puts the backup in place.
    QObject::connect(&controller, &app::OperatorController::restartRequested, &app, [&app, &controller] {
        controller.saveSession();
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
        app.quit();
    });

    // Scripture: the bundled Bibles (read-only) and any the user adds.
    std::unique_ptr<scripture::BibleStore> bundledBibleStore;
    if (const QString path = bundledBibles(); !path.isEmpty()) {
        if (auto store = scripture::BibleStore::open(path, true); store.ok())
            bundledBibleStore = std::move(store).value();
        else
            qWarning("Bundled Bibles not opened: %s", qPrintable(store.error().message));
    } else {
        qWarning("Bundled Bibles not found (bibles.db)");
    }
    std::unique_ptr<scripture::BibleStore> userBibleStore;
    if (!parser.isSet(designPreview)) {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (auto store = scripture::BibleStore::open(QDir(folder).filePath(QStringLiteral("user-bibles.db")), false); store.ok())
            userBibleStore = std::move(store).value();
    } else if (auto store = scripture::BibleStore::open(QStringLiteral(":memory:"), false); store.ok()) {
        userBibleStore = std::move(store).value();
    }
    app::ScriptureController scriptureController(controller);
    scriptureController.setStores(bundledBibleStore.get(), userBibleStore.get());
    app::ThemeController themeController(controller);
    app::TimerController timerController(controller, show);
    timerController.attach(*library);
    app::PropsController propsController(show);
    propsController.attach(*library);
    app::MessageTemplates messageTemplates(controller);
    messageTemplates.attach(*library);
    app::EditorController editorController(controller);
    editorController.attach(*library, &songLibrary);
    themeController.attach(*library);

    // Media: thumbnails go beside the library (a temporary folder for the
    // design preview, so it leaves nothing behind).
    QTemporaryDir previewThumbnails;
    const QString thumbnailFolder =
        parser.isSet(designPreview)
            ? previewThumbnails.path()
            : QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("thumbnails"));
    app::MediaLibrary mediaLibrary(*library, thumbnailFolder);
    controller.attachMedia(mediaLibrary);

    // Slide decks: one picture per slide, in a folder per deck.
    QTemporaryDir previewSlides;
    const QString slideFolder =
        parser.isSet(designPreview)
            ? previewSlides.path()
            : QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("slides"));
    app::PresentationLibrary presentationLibrary(*library, slideFolder);
    controller.attachPresentations(presentationLibrary);

    std::unique_ptr<Output> output;
    const auto turnOutputOn = [&] {
        if (output)
            return;
        output = startOutput(show, parser.value(renderServer), parser.value(renderExecutable),
                             parser.values(renderOption));
        controller.setOutputConnected(output != nullptr);
        // The video Space would send next is opened ahead of time, so it
        // starts at once when it goes live.
        if (output) {
            QObject::connect(&controller, &app::OperatorController::previewVideoChanged, output->bridge.get(),
                             &show::OutputBridge::preload);
        }
    };
    const bool outputWanted = parser.isSet(noOutputOption) ? false
                              : parser.isSet(outputOption)  ? true
                              : !parser.isSet(designPreview) && settings.value(QLatin1String(outputEnabledKey), true).toBool();
    if (outputWanted)
        turnOutputOn();
    // The Main light in the toolbar turns the output off and on (and is
    // remembered for the next start).
    QObject::connect(&controller, &app::OperatorController::outputToggleRequested, &app, [&](bool on) {
        if (on) {
            turnOutputOn();
        } else if (output) {
            (void)output->link->quit();
            output.reset();
            controller.setOutputConnected(false);
        }
        if (!parser.isSet(designPreview))
            QSettings().setValue(QLatin1String(outputEnabledKey), on);
    });

    // The stage display (batch 13): off by default; the Stage popover turns
    // it on and picks its layout.
    app::StageController stageController(controller, timerController);
    stageController.setLayout(settings.value(QLatin1String(stageLayoutKey), 0).toInt());
    QObject::connect(&stageController, &app::StageController::changed, &app, [&stageController, &parser, &designPreview] {
        if (!parser.isSet(designPreview))
            QSettings().setValue(QLatin1String(stageLayoutKey), stageController.layout());
    });
    std::unique_ptr<render::RenderLink> stage;
    const auto turnStageOn = [&] {
        if (stage)
            return;
        stage = startStage(parser.value(renderExecutable), parser.value(renderServer));
        if (stage) {
            render::RenderLink *link = stage.get();
            stageController.setSender([link](const QJsonObject &state) {
                (void)link->send({{QStringLiteral("cmd"), QStringLiteral("stage")}, {QStringLiteral("stage"), state}});
            });
        }
        stageController.setConnected(stage != nullptr);
    };
    if (!parser.isSet(designPreview) && settings.value(QLatin1String(stageEnabledKey), false).toBool())
        turnStageOn();
    QObject::connect(&stageController, &app::StageController::toggleRequested, &app, [&](bool on) {
        if (on) {
            turnStageOn();
        } else if (stage) {
            stageController.setConnected(false);
            stageController.setSender({});
            (void)stage->quit();
            stage.reset();
        }
        if (!parser.isSet(designPreview))
            QSettings().setValue(QLatin1String(stageEnabledKey), on);
    });

    // The stream output (batch 15): Slide and Props on transparency, as key
    // and fill and/or NDI, from a render process of its own that follows the
    // show (it never hands its scene back).
    app::StreamController streamController(settings.value(QLatin1String(experimentalNdiKey), false).toBool());
    streamController.setKeyFill(settings.value(QLatin1String(streamKeyFillKey), true).toBool());
    streamController.setNdi(settings.value(QLatin1String(streamNdiKey), false).toBool());
    streamController.setNdiName(settings.value(QLatin1String(streamNdiNameKey), QStringLiteral("Lumen Presenter")).toString());
    struct Stream
    {
        std::unique_ptr<render::RenderLink> link;
        std::unique_ptr<show::OutputBridge> bridge;
    };
    std::unique_ptr<Stream> stream;
    const auto stopStream = [&] {
        if (!stream)
            return;
        stream->bridge.reset();
        (void)stream->link->quit();
        stream.reset();
        streamController.setConnected(false);
    };
    const auto startStream = [&] {
        if (stream)
            return;
        render::RenderLink::Options options;
        options.serverName = (parser.value(renderServer).isEmpty() ? render::defaultServerName() : parser.value(renderServer))
                             + QStringLiteral("-stream");
        options.executable = parser.value(renderExecutable);
        options.arguments = streamController.renderArguments();
        const QList<QScreen *> screens = QGuiApplication::screens();
        QList<int> others;
        for (int i = 0; i < screens.size(); ++i) {
            if (screens.at(i) != QGuiApplication::primaryScreen())
                others << i;
        }
        // Key and fill go fullscreen on two spare screens if there are any
        // (after the audience output's and the stage's), else in windows.
        if (streamController.keyFill() && others.size() >= 4)
            options.arguments << QStringLiteral("--fill-screen=%1").arg(others.at(2)) << QStringLiteral("--key-screen=%1").arg(others.at(3));
        auto started = std::make_unique<Stream>();
        started->link = std::make_unique<render::RenderLink>(options);
        if (auto ok = started->link->start(); !ok.ok()) {
            streamController.setConnected(false, QObject::tr("The stream output did not start: %1").arg(ok.error().message));
            return;
        }
        started->bridge = std::make_unique<show::OutputBridge>(show, *started->link);
        started->bridge->follow();
        auto status = started->link->request({{QStringLiteral("cmd"), QStringLiteral("status")}});
        stream = std::move(started);
        streamController.setConnected(true, status.ok() ? status.value().value(QStringLiteral("stream")).toString() : QString());
    };
    if (!parser.isSet(designPreview) && settings.value(QLatin1String(streamEnabledKey), false).toBool())
        startStream();
    QObject::connect(&streamController, &app::StreamController::toggleRequested, &app, [&](bool on) {
        on ? startStream() : stopStream();
        if (!parser.isSet(designPreview))
            QSettings().setValue(QLatin1String(streamEnabledKey), on);
    });
    QObject::connect(&streamController, &app::StreamController::restartRequested, &app, [&] {
        stopStream();
        startStream();
    });
    QObject::connect(&streamController, &app::StreamController::changed, &app, [&] {
        if (parser.isSet(designPreview))
            return;
        QSettings stored;
        stored.setValue(QLatin1String(streamKeyFillKey), streamController.keyFill());
        stored.setValue(QLatin1String(streamNdiKey), streamController.ndi());
        stored.setValue(QLatin1String(streamNdiNameKey), streamController.ndiName());
    });

    // A normal quit closes the outputs; a crash leaves them showing.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&output, &stage, &stream] {
        if (stream)
            (void)stream->link->quit();
        if (stage)
            (void)stage->quit();
        if (output)
            (void)output->link->quit();
    });

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue<QObject *>(&controller)},
                                 {QStringLiteral("scripture"), QVariant::fromValue<QObject *>(&scriptureController)},
                                 {QStringLiteral("themes"), QVariant::fromValue<QObject *>(&themeController)},
                                 {QStringLiteral("timerController"), QVariant::fromValue<QObject *>(&timerController)},
                                 {QStringLiteral("stageController"), QVariant::fromValue<QObject *>(&stageController)},
                                 {QStringLiteral("propsController"), QVariant::fromValue<QObject *>(&propsController)},
                                 {QStringLiteral("messageTemplates"), QVariant::fromValue<QObject *>(&messageTemplates)},
                                 {QStringLiteral("streamController"), QVariant::fromValue<QObject *>(&streamController)},
                                 {QStringLiteral("editorController"), QVariant::fromValue<QObject *>(&editorController)}});
    engine.loadFromModule("Lumen.Ui", "Main");

    if (parser.isSet(quitAfter))
        QTimer::singleShot(parser.value(quitAfter).toInt(), &app, &QCoreApplication::quit);
    return QGuiApplication::exec();
}
