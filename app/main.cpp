#include "operatorcontroller.h"
#include "scripturecontroller.h"
#include "servicedata.h"
#include "themecontroller.h"
#include "themes.h"
#include "editorcontroller.h"
#include "messagetemplates.h"
#include "sharing.h"
#include "cuecontroller.h"
#include "livecontrol.h"
#include "midiinput.h"
#include "oscserver.h"
#include "remoteserver.h"
#include "propscontroller.h"
#include "screensetup.h"
#include "updatechecker.h"
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
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QScreen>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QSysInfo>
#include <QStandardPaths>
#include <QLocalServer>
#include <QLocalSocket>
#include <QWindow>
#include <QProcess>
#include <QTimer>
#include <QFile>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <functional>
#include <memory>

Q_IMPORT_QML_PLUGIN(Lumen_UiPlugin)
Q_IMPORT_QML_PLUGIN(Lumen_SlidesPlugin)

using namespace lumen;
using namespace lumen::core;

namespace {

constexpr auto outputEnabledKey = "output/enabled"; // on by default since batch 12
constexpr auto stageEnabledKey = "stage/enabled";   // off by default (needs a screen)
constexpr auto stageLayoutKey = "stage/layout";
constexpr auto sharingEnabledKey = "sharing/enabled"; // off by default (batch 18)
constexpr auto remoteEnabledKey = "remote/enabled";   // batch 19, all off by default
constexpr auto remotePortKey = "remote/port";
constexpr auto oscEnabledKey = "osc/enabled";
constexpr auto oscPortKey = "osc/port";
constexpr auto midiEnabledKey = "midi/enabled";
constexpr auto midiMappingKey = "midi/mapping";
// Timecode cues (batch 20): off by default, they confused operators who
// only want slides and videos.
constexpr auto timecodeCuesKey = "experimental/timecodeCues";
constexpr auto streamEnabledKey = "stream/enabled";   // off by default
constexpr auto streamKeyFillKey = "stream/keyFill";
constexpr auto streamNdiKey = "stream/ndi";
constexpr auto streamNdiNameKey = "stream/ndiName";
constexpr auto experimentalNdiKey = "experimental/ndi"; // untested: off by default
// The slide editor: on by default since batch 16 (set to false to hide Edit).
constexpr auto experimentalEditorKey = "experimental/editor";
constexpr auto linesPerSlideKey = "songs/linesPerSlide"; // auto-split limit, 2 by default
constexpr auto backupFolderKey = "backups/folder"; // default: "backups" beside the library

// The link to the render process and the bridge that feeds it the scene.
struct Output
{
    std::unique_ptr<render::RenderLink> link;
    std::unique_ptr<show::OutputBridge> bridge;
};

// The stage display's renderer (batch 13): on the screen the screen setup
// gives it, else in a window. Returns its link, or null if it could not start.
std::unique_ptr<render::RenderLink> startStage(const QString &executable, const QString &audienceServer, int screen,
                                               const QStringList &extraArguments)
{
    render::RenderLink::Options options;
    options.serverName = (audienceServer.isEmpty() ? render::defaultServerName() : audienceServer) + QStringLiteral("-stage");
    options.executable = executable;
    options.arguments = QStringList{QStringLiteral("--stage"),
                                    screen >= 0 ? QStringLiteral("--screen=%1").arg(screen) : QStringLiteral("--windowed")}
                        + extraArguments;
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

// The library database, in the app's data folder. If it cannot be opened the
// app still runs, with an empty in-memory library.
std::unique_ptr<data::Database> openLibrary()
{
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
    auto memory = data::Database::open(data::Database::inMemory());
    if (!memory.ok()) {
        qCritical("No database available: %s", qPrintable(memory.error().message));
        return nullptr;
    }
    return std::move(memory).value();
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

// --check-install: whether the operator window loaded, the bundled Bibles
// open, and the renderer starts and draws. Prints one line per check.
int checkInstallation(QQmlApplicationEngine &engine, scripture::BibleStore *bibles,
                      const std::function<render::RenderLink *()> &startOutput)
{
    int failures = 0;
    const auto report = [&failures](bool ok, const QString &what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
        std::fflush(stdout);
        if (!ok)
            ++failures;
    };
    report(!engine.rootObjects().isEmpty(), QStringLiteral("operator window loaded"));

    std::size_t translations = 0;
    if (bibles) {
        if (auto list = bibles->translations(); list.ok())
            translations = list.value().size();
    }
    report(translations > 0, QStringLiteral("bundled Bibles: %1 translations (%2)")
                                  .arg(translations)
                                  .arg(bundledBibles().isEmpty() ? QStringLiteral("bibles.db not found")
                                                                 : QDir::toNativeSeparators(bundledBibles())));

    const QString renderer = render::RenderLink::findExecutable();
    report(!renderer.isEmpty(), QStringLiteral("renderer: %1").arg(renderer.isEmpty() ? QStringLiteral("not found")
                                                                                      : QDir::toNativeSeparators(renderer)));
    render::RenderLink *link = renderer.isEmpty() ? nullptr : startOutput();
    report(link != nullptr, QStringLiteral("audience output started"));
    qint64 frames = 0;
    if (link) {
        QDeadlineTimer deadline(15000);
        while (frames == 0 && !deadline.hasExpired()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            if (auto status = link->request({{QStringLiteral("cmd"), QStringLiteral("status")}}, 2000); status.ok())
                frames = status.value().value(QStringLiteral("frames")).toInteger();
        }
        (void)link->quit();
    }
    report(frames > 0, QStringLiteral("audience output drew %1 frames").arg(frames));
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
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
    // The output starts with the app (setting output/enabled, on by
    // default); --output starts it even when the setting is off.
    // --experimental-output is the name it had before batch 12.
    const QCommandLineOption outputOption(
        QStringList{QStringLiteral("output"), QStringLiteral("experimental-output")},
        QStringLiteral("Start the audience output (the default)."));
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
    // For installers: checks the install works (window, Bibles, renderer and
    // a first output frame), prints what it found and exits.
    const QCommandLineOption checkInstall(QStringLiteral("check-install"),
                                          QStringLiteral("Check that this installation works, then exit."));
    parser.addOptions({outputOption, noOutputOption, renderServer, renderExecutable, renderOption, quitAfter, checkInstall});
    parser.process(app);
    const bool checking = parser.isSet(checkInstall);
    // A check uses Qt's test folders, never the user's library or settings.
    if (checking)
        QStandardPaths::setTestModeEnabled(true);
    // A check never connects to the output of a Lumen that is already running.
    const QString outputServer = checking && !parser.isSet(renderServer)
                                     ? QStringLiteral("lumen-check-%1").arg(QCoreApplication::applicationPid())
                                     : parser.value(renderServer);

    // One Lumen at a time (per output): a second start brings the running
    // window forward and quits, rather than share (and later close) the same
    // audience output.
    const QString instanceName = (outputServer.isEmpty() ? render::defaultServerName() : outputServer) + QStringLiteral("-ui");
    QLocalServer instance;
    if (!checking) {
        QLocalSocket running;
        running.connectToServer(instanceName);
        if (running.waitForConnected(300)) {
            running.write("show\n");
            running.flush();
            running.waitForBytesWritten(300);
            std::printf("Lumen Presenter is already running; its window was brought forward.\n");
            return EXIT_SUCCESS;
        }
        QLocalServer::removeServer(instanceName); // left by a crash
        if (!instance.listen(instanceName))
            qWarning("Single-instance check unavailable: %s", qPrintable(instance.errorString()));
    }

    const QSettings settings;
    show::ShowController show;
    auto library = openLibrary();
    if (!library)
        return EXIT_FAILURE;

    if (auto themed = app::ensureDefaultThemes(*library); !themed.ok())
        qWarning("Default themes not set up: %s", qPrintable(themed.error().message));

    app::OperatorController controller(show, settings.value(QLatin1String(experimentalEditorKey), true).toBool());
    songs::SongLibrary songLibrary(*library, settings.value(QLatin1String(linesPerSlideKey), 2).toInt());
    if (auto indexed = songLibrary.indexMissing(); !indexed.ok())
        qWarning("Song search index not updated: %s", qPrintable(indexed.error().message));
    controller.attachLibrary(*library, songLibrary);

    // Backups (daily, the newest 14) and the session saved every 30 s. An
    // in-memory library (the user's could not be opened) has neither.
    const QString dataFolder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    std::unique_ptr<app::Backups> backups;
    QTimer backupTimer;
    if (library->path() != data::Database::inMemory()) {
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
    {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (auto store = scripture::BibleStore::open(QDir(folder).filePath(QStringLiteral("user-bibles.db")), false); store.ok())
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

    // Sharing with other Lumen computers on the local network (batch 18):
    // off unless the operator turned it on (remembered).
    app::Sharing sharing(*library, QSysInfo::machineHostName(),
                         QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("imported")));
    if (settings.value(QLatin1String(sharingEnabledKey), false).toBool())
        sharing.setEnabled(true);
    QObject::connect(&sharing, &app::Sharing::changed, &app, [&sharing] {
        QSettings().setValue(QLatin1String(sharingEnabledKey), sharing.enabled());
    });
    QObject::connect(&sharing, &app::Sharing::imported, &controller, &app::OperatorController::refreshLibrary);

    // Remote control (batch 19): the phone remote, OSC and MIDI, each off
    // until turned on (remembered), all through the same live commands.
    // Timecode cues (batch 20).
    app::CueController cueController(controller);
    cueController.attach(*library);

    app::LiveControl liveControl(controller);
    app::RemoteServer remote(liveControl, quint16(settings.value(QLatin1String(remotePortKey), 8180).toInt()));
    app::OscServer osc(liveControl, quint16(settings.value(QLatin1String(oscPortKey), 8000).toInt()));
    app::MidiInput midi(liveControl);
    midi.setMappingsJson(QJsonDocument::fromJson(settings.value(QLatin1String(midiMappingKey)).toByteArray()).array());
    remote.setEnabled(settings.value(QLatin1String(remoteEnabledKey), false).toBool());
    osc.setEnabled(settings.value(QLatin1String(oscEnabledKey), false).toBool());
    midi.setEnabled(settings.value(QLatin1String(midiEnabledKey), false).toBool());
    QObject::connect(&remote, &app::RemoteServer::changed, &app, [&remote] {
        QSettings().setValue(QLatin1String(remoteEnabledKey), remote.enabled());
    });
    QObject::connect(&osc, &app::OscServer::changed, &app, [&osc] {
        QSettings().setValue(QLatin1String(oscEnabledKey), osc.enabled());
    });
    QObject::connect(&midi, &app::MidiInput::changed, &app, [&midi] {
        QSettings().setValue(QLatin1String(midiEnabledKey), midi.enabled());
    });
    QObject::connect(&midi, &app::MidiInput::mappingsChanged, &app, [&midi] {
        QSettings().setValue(QLatin1String(midiMappingKey), QJsonDocument(midi.mappingsJson()).toJson(QJsonDocument::Compact));
    });
    themeController.attach(*library);

    // Media: thumbnails go beside the library.
    const QString thumbnailFolder =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("thumbnails"));
    app::MediaLibrary mediaLibrary(*library, thumbnailFolder);
    controller.attachMedia(mediaLibrary);

    // Slide decks: one picture per slide, in a folder per deck.
    const QString slideFolder =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("slides"));
    app::PresentationLibrary presentationLibrary(*library, slideFolder);
    controller.attachPresentations(presentationLibrary);

    // Which screen shows what (the Screens setup): remembered for each set of
    // screens; a set seen for the first time opens the setup.
    app::ScreenSetup screenSetup;
    // Where the screen setup puts an output: fullscreen on its screen, or a
    // window when several screens are connected but none has the role.
    const auto screenArguments = [&screenSetup](int index) -> QStringList {
        if (index >= 0)
            return {QStringLiteral("--screen=%1").arg(index)};
        return screenSetup.count() > 1 ? QStringList{QStringLiteral("--windowed")} : QStringList{};
    };
    const auto screenCommand = [&screenSetup](int index) {
        return index >= 0 ? QJsonObject{{QStringLiteral("cmd"), QStringLiteral("screen")}, {QStringLiteral("index"), index}}
                          : QJsonObject{{QStringLiteral("cmd"), QStringLiteral("screen")}, {QStringLiteral("windowed"), true}};
    };

    std::unique_ptr<Output> output;
    const auto turnOutputOn = [&] {
        if (output)
            return;
        // The setup's screen first, so a --render-option=--screen=N given by
        // hand still wins.
        output = startOutput(show, outputServer, parser.value(renderExecutable),
                             screenArguments(screenSetup.mainScreen()) + screenSetup.rendererArguments()
                                 + parser.values(renderOption));
        // A renderer that kept running from before may be on another screen.
        if (output && !parser.isSet(renderOption) && (screenSetup.mainScreen() >= 0 || screenSetup.count() > 1))
            (void)output->link->send(screenCommand(screenSetup.mainScreen()));
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
                              : settings.value(QLatin1String(outputEnabledKey), true).toBool();
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
        QSettings().setValue(QLatin1String(outputEnabledKey), on);
    });

    // The stage display (batch 13): off by default; the Stage popover turns
    // it on and picks its layout.
    app::StageController stageController(controller, timerController);
    stageController.setLayout(settings.value(QLatin1String(stageLayoutKey), 0).toInt());
    QObject::connect(&stageController, &app::StageController::changed, &app, [&stageController] {
        QSettings().setValue(QLatin1String(stageLayoutKey), stageController.layout());
    });
    std::unique_ptr<render::RenderLink> stage;
    const auto turnStageOn = [&] {
        if (stage)
            return;
        stage = startStage(parser.value(renderExecutable), parser.value(renderServer), screenSetup.stageScreen(),
                           screenSetup.rendererArguments());
        if (stage) {
            (void)stage->send(screenCommand(screenSetup.stageScreen()));
            render::RenderLink *link = stage.get();
            stageController.setSender([link](const QJsonObject &state) {
                (void)link->send({{QStringLiteral("cmd"), QStringLiteral("stage")}, {QStringLiteral("stage"), state}});
            });
        }
        stageController.setConnected(stage != nullptr);
    };
    if (settings.value(QLatin1String(stageEnabledKey), false).toBool())
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
    if (settings.value(QLatin1String(streamEnabledKey), false).toBool())
        startStream();
    QObject::connect(&streamController, &app::StreamController::toggleRequested, &app, [&](bool on) {
        on ? startStream() : stopStream();
        QSettings().setValue(QLatin1String(streamEnabledKey), on);
    });
    QObject::connect(&streamController, &app::StreamController::restartRequested, &app, [&] {
        stopStream();
        startStream();
    });
    QObject::connect(&streamController, &app::StreamController::changed, &app, [&] {
        QSettings stored;
        stored.setValue(QLatin1String(streamKeyFillKey), streamController.keyFill());
        stored.setValue(QLatin1String(streamNdiKey), streamController.ndi());
        stored.setValue(QLatin1String(streamNdiNameKey), streamController.ndiName());
    });

    // The Screens setup: labels on every screen while it is open, and the
    // outputs follow the roles as they change.
    const auto showLabels = [&] {
        screenSetup.setRunning(output != nullptr, stage != nullptr);
        const bool open = screenSetup.isOpen();
        if (output) {
            (void)output->link->send({{QStringLiteral("cmd"), QStringLiteral("identify")},
                                      {QStringLiteral("identify"),
                                       open ? screenSetup.label(screenSetup.mainScreen()) : QJsonObject()}});
        }
        if (stage) {
            (void)stage->send({{QStringLiteral("cmd"), QStringLiteral("identify")},
                               {QStringLiteral("identify"),
                                open ? screenSetup.label(screenSetup.stageScreen()) : QJsonObject()}});
        }
    };
    int stageScreenBefore = screenSetup.stageScreen();
    QObject::connect(&screenSetup, &app::ScreenSetup::rolesChanged, &app, [&] {
        if (output)
            (void)output->link->send(screenCommand(screenSetup.mainScreen()));
        const int stageScreen = screenSetup.stageScreen();
        if (stageScreen >= 0 && !stage) {
            // Giving a screen the stage role turns the stage display on.
            turnStageOn();
            QSettings().setValue(QLatin1String(stageEnabledKey), true);
        } else if (stageScreen >= 0 && stage) {
            (void)stage->send(screenCommand(stageScreen));
        } else if (stageScreen < 0 && stageScreenBefore >= 0 && stage) {
            // Its screen was given to something else: the stage display goes off.
            stageController.setConnected(false);
            stageController.setSender({});
            (void)stage->quit();
            stage.reset();
            QSettings().setValue(QLatin1String(stageEnabledKey), false);
        }
        stageScreenBefore = stageScreen;
        showLabels();
    });
    QObject::connect(&screenSetup, &app::ScreenSetup::openChanged, &app, showLabels);
    QObject::connect(&controller, &app::OperatorController::stateChanged, &app, [&] {
        screenSetup.setRunning(output != nullptr, stage != nullptr);
    });
    QObject::connect(&stageController, &app::StageController::changed, &app, [&] {
        screenSetup.setRunning(output != nullptr, stage != nullptr);
    });
    // Through XWayland or not: the outputs start again to take it.
    QObject::connect(&screenSetup, &app::ScreenSetup::placementModeChanged, &app, [&] {
        if (output) {
            (void)output->link->quit();
            output.reset();
            turnOutputOn();
        }
        if (stage) {
            stageController.setConnected(false);
            stageController.setSender({});
            (void)stage->quit();
            stage.reset();
            turnStageOn();
        }
        showLabels();
    });
    // How the outputs are doing, asked once a second without ever waiting:
    // videos they could not play, and the screens they are really on.
    QString actualMain;
    QString actualStage;
    QTimer outputHealth;
    outputHealth.setInterval(1000);
    QObject::connect(&outputHealth, &QTimer::timeout, &app, [&] {
        if (output) {
            (void)output->link->ask({{QStringLiteral("cmd"), QStringLiteral("status")}}, [&](const QJsonObject &status) {
                controller.setOutputStatus(status);
                actualMain = status.value(QStringLiteral("screen")).toString();
                screenSetup.setActualScreens(actualMain, actualStage);
            });
        } else {
            actualMain.clear();
            controller.setOutputStatus({});
        }
        if (stage) {
            (void)stage->ask({{QStringLiteral("cmd"), QStringLiteral("status")}}, [&](const QJsonObject &status) {
                actualStage = status.value(QStringLiteral("screen")).toString();
                screenSetup.setActualScreens(actualMain, actualStage);
            });
        } else {
            actualStage.clear();
        }
        screenSetup.setActualScreens(actualMain, actualStage);
    });
    if (!checking)
        outputHealth.start();

    // A screen plugged in or out: a new set of screens opens the setup.
    const auto screensChanged = [&] {
        screenSetup.refresh();
        if (!checking && screenSetup.needsSetup())
            screenSetup.openSetup();
        showLabels();
    };
    QObject::connect(&app, &QGuiApplication::screenAdded, &app, screensChanged);
    QObject::connect(&app, &QGuiApplication::screenRemoved, &app, screensChanged);

    // A normal quit closes the outputs; a crash leaves them showing.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&output, &stage, &stream] {
        if (stream)
            (void)stream->link->quit();
        if (stage)
            (void)stage->quit();
        if (output)
            (void)output->link->quit();
    });

    // Optional update checks: none unless the operator turned on "Check when
    // Lumen starts" (then once, a little after starting) or checks by hand.
    app::UpdateChecker updates;
    if (!checking && updates.automatic())
        QTimer::singleShot(10000, &updates, &app::UpdateChecker::check);

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
                                 {QStringLiteral("editorController"), QVariant::fromValue<QObject *>(&editorController)},
                                 {QStringLiteral("sharing"), QVariant::fromValue<QObject *>(&sharing)},
                                 {QStringLiteral("remote"), QVariant::fromValue<QObject *>(&remote)},
                                 {QStringLiteral("osc"), QVariant::fromValue<QObject *>(&osc)},
                                 {QStringLiteral("midi"), QVariant::fromValue<QObject *>(&midi)},
                                 // Timecode cues: kept, but out of the way unless turned on.
                                 {QStringLiteral("cues"), settings.value(QLatin1String(timecodeCuesKey), false).toBool()
                                                              ? QVariant::fromValue<QObject *>(&cueController)
                                                              : QVariant::fromValue<QObject *>(nullptr)},
                                 {QStringLiteral("screenSetup"), QVariant::fromValue<QObject *>(&screenSetup)},
                                 {QStringLiteral("updates"), QVariant::fromValue<QObject *>(&updates)}});
    engine.loadFromModule("Lumen.Ui", "Main");

    // Another start of Lumen asked for this window.
    QObject::connect(&instance, &QLocalServer::newConnection, &app, [&] {
        while (QLocalSocket *other = instance.nextPendingConnection()) {
            QObject::connect(other, &QLocalSocket::disconnected, other, &QObject::deleteLater);
            if (auto *window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QWindow *>(engine.rootObjects().first())) {
                window->show();
                window->raise();
                window->requestActivate();
            }
        }
    });

    // Screens seen for the first time: ask which is which.
    if (!checking && screenSetup.needsSetup())
        screenSetup.openSetup();

    if (checking)
        return checkInstallation(engine, bundledBibleStore.get(), [&]() -> render::RenderLink * {
            turnOutputOn();
            return output ? output->link.get() : nullptr;
        });

    if (parser.isSet(quitAfter))
        QTimer::singleShot(parser.value(quitAfter).toInt(), &app, &QCoreApplication::quit);
    return QGuiApplication::exec();
}
