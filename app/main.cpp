#include "operatorcontroller.h"
#include "servicedata.h"

#include "lumen/core/buildinfo.h"
#include "lumen/core/data/database.h"
#include "lumen/core/render/renderlink.h"
#include "lumen/core/show/outputbridge.h"
#include "lumen/core/show/showcontroller.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Lumen_UiPlugin)

using namespace lumen;
using namespace lumen::core;

namespace {

constexpr auto experimentalOutputKey = "experimental/output";
constexpr auto experimentalEditorKey = "experimental/editor";

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

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(appName());
    QGuiApplication::setApplicationDisplayName(appName());
    QGuiApplication::setOrganizationName(organisationName());
    QGuiApplication::setApplicationVersion(version());

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

    app::OperatorController controller(show, settings.value(QLatin1String(experimentalEditorKey), false).toBool());
    if (auto plan = app::loadLatestPlan(*library); plan.ok())
        controller.setPlan(plan.value());
    else
        qWarning("Plan not loaded: %s", qPrintable(plan.error().message));
    if (auto songs = app::loadSongs(*library); songs.ok())
        controller.setSongs(songs.value());

    std::unique_ptr<Output> output;
    if (parser.isSet(experimentalOutput) || settings.value(QLatin1String(experimentalOutputKey), false).toBool()) {
        output = startOutput(show, parser.value(renderServer), parser.value(renderExecutable),
                             parser.values(renderOption));
        controller.setOutputConnected(output != nullptr);
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
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue<QObject *>(&controller)}});
    engine.loadFromModule("Lumen.Ui", "Main");

    return QGuiApplication::exec();
}
