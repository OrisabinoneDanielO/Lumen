#include "lumen/core/buildinfo.h"

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

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule("Lumen.Ui", "Main");

    return QGuiApplication::exec();
}
