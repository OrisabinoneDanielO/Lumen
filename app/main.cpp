#include "lumen/core/buildinfo.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Lumen_UiPlugin)

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
    parser.addOption(designPreview);
    parser.process(app);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.setInitialProperties({{QStringLiteral("designPreview"), parser.isSet(designPreview)}});
    engine.loadFromModule("Lumen.Ui", "Main");

    return QGuiApplication::exec();
}
