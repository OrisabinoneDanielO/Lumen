#include "lumen/core/buildinfo.h"
#include "lumen/core/render/renderlink.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Lumen_UiPlugin)

namespace {

constexpr auto experimentalOutputKey = "experimental/output";

// The render process is unfinished (it shows a test frame), so it only starts
// when the experimental output setting or flag is on. Off by default.
std::unique_ptr<lumen::core::render::RenderLink> startOutput(const QString &serverName,
                                                             const QString &executable,
                                                             const QStringList &renderOptions)
{
    lumen::core::render::RenderLink::Options options;
    if (!serverName.isEmpty())
        options.serverName = serverName;
    options.executable = executable;
    options.arguments = renderOptions;

    auto link = std::make_unique<lumen::core::render::RenderLink>(options);
    if (auto started = link->start(); !started.ok()) {
        qWarning("Output not started: %s", qPrintable(started.error().message));
        return nullptr;
    }
    if (auto shown = link->show(QColor(0x14, 0x21, 0x3D), lumen::core::appName()); !shown.ok())
        qWarning("Output test frame not shown: %s", qPrintable(shown.error().message));
    return link;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(lumen::core::appName());
    QGuiApplication::setApplicationDisplayName(lumen::core::appName());
    QGuiApplication::setOrganizationName(lumen::core::organisationName());
    QGuiApplication::setApplicationVersion(lumen::core::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(lumen::core::appName());
    parser.addHelpOption();
    parser.addVersionOption();
    // Off by default: shows the mockup's sample content instead of empty
    // states, for comparing the window with design/mockups/Main.html.
    const QCommandLineOption designPreview(
        QStringLiteral("design-preview"),
        QStringLiteral("Fill the window with the mockup's sample content (no real data)."));
    const QCommandLineOption experimentalOutput(
        QStringLiteral("experimental-output"),
        QStringLiteral("Start the render process and show a test frame (unfinished; also the "
                       "experimental/output setting)."));
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

    std::unique_ptr<lumen::core::render::RenderLink> output;
    const bool outputEnabled = parser.isSet(experimentalOutput)
                               || QSettings().value(QLatin1String(experimentalOutputKey), false).toBool();
    if (outputEnabled) {
        output = startOutput(parser.value(renderServer), parser.value(renderExecutable),
                             parser.values(renderOption));
    }
    // A normal quit closes the output; a crash leaves it showing the last frame.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&output] {
        if (output)
            (void)output->quit();
    });

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.setInitialProperties({{QStringLiteral("designPreview"), parser.isSet(designPreview)}});
    engine.loadFromModule("Lumen.Ui", "Main");

    return QGuiApplication::exec();
}
