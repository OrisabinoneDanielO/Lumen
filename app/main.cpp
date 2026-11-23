#include "operatorcontroller.h"
#include "scripturecontroller.h"
#include "servicedata.h"
#include "themecontroller.h"
#include "themes.h"

#include "lumen/core/buildinfo.h"
#include "lumen/core/data/database.h"
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
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Lumen_UiPlugin)
Q_IMPORT_QML_PLUGIN(Lumen_SlidesPlugin)

using namespace lumen;
using namespace lumen::core;

namespace {

constexpr auto experimentalOutputKey = "experimental/output";
constexpr auto experimentalEditorKey = "experimental/editor";
constexpr auto linesPerSlideKey = "songs/linesPerSlide"; // auto-split limit, 2 by default

// The link to the render process and the bridge that feeds it the scene.
struct Output
{
    std::unique_ptr<render::RenderLink> link;
    std::unique_ptr<show::OutputBridge> bridge;
};

// The render process has no themes yet, so it only starts when the
// experimental output setting or flag is on. Off by default.
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
        auto db = data::Database::open(QDir(folder).filePath(QStringLiteral("library.db")));
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
    const QCommandLineOption experimentalOutput(
        QStringLiteral("experimental-output"),
        QStringLiteral("Start the render process (unfinished; also the experimental/output setting)."));
    const QCommandLineOption renderServer(QStringLiteral("render-server"),
                                          QStringLiteral("Local socket name of the renderer."),
                                          QStringLiteral("name"));
    const QCommandLineOption renderExecutable(QStringLiteral("render-executable"),
                                              QStringLiteral("Path to lumen-render."),
                                              QStringLiteral("path"));
    const QCommandLineOption renderOption(QStringLiteral("render-option"),
                                          QStringLiteral("Extra argument for lumen-render (repeatable)."),
                                          QStringLiteral("argument"));
    parser.addOptions({designPreview, experimentalOutput, renderServer, renderExecutable, renderOption});
    parser.process(app);

    const QSettings settings;
    show::ShowController show;
    auto library = openLibrary(parser.isSet(designPreview));
    if (!library)
        return EXIT_FAILURE;

    if (auto themed = app::ensureDefaultThemes(*library); !themed.ok())
        qWarning("Default themes not set up: %s", qPrintable(themed.error().message));

    app::OperatorController controller(show, settings.value(QLatin1String(experimentalEditorKey), false).toBool());
    if (auto plan = app::loadLatestPlan(*library); plan.ok())
        controller.setPlan(plan.value());
    else
        qWarning("Plan not loaded: %s", qPrintable(plan.error().message));
    songs::SongLibrary songLibrary(*library, settings.value(QLatin1String(linesPerSlideKey), 2).toInt());
    if (auto indexed = songLibrary.indexMissing(); !indexed.ok())
        qWarning("Song search index not updated: %s", qPrintable(indexed.error().message));
    controller.attachLibrary(*library, songLibrary);

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
    if (parser.isSet(experimentalOutput) || settings.value(QLatin1String(experimentalOutputKey), false).toBool()) {
        output = startOutput(show, parser.value(renderServer), parser.value(renderExecutable),
                             parser.values(renderOption));
        controller.setOutputConnected(output != nullptr);
        // The video Space would send next is opened ahead of time, so it
        // starts at once when it goes live.
        if (output) {
            QObject::connect(&controller, &app::OperatorController::previewVideoChanged, output->bridge.get(),
                             &show::OutputBridge::preload);
        }
    }
    // A normal quit closes the output; a crash leaves it showing the last frame.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&output] {
        if (output)
            (void)output->link->quit();
    });

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue<QObject *>(&controller)},
                                 {QStringLiteral("scripture"), QVariant::fromValue<QObject *>(&scriptureController)},
                                 {QStringLiteral("themes"), QVariant::fromValue<QObject *>(&themeController)}});
    engine.loadFromModule("Lumen.Ui", "Main");

    return QGuiApplication::exec();
}
