#include "outputwindow.h"
#include "renderserver.h"
#include "screenchoice.h"

#include "lumen/core/buildinfo.h"
#include "lumen/core/render/protocol.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QScreen>

#include <optional>

// The render process. It owns the audience output window and keeps
// presenting frames whatever happens to the operator UI: if the UI crashes,
// the last frame stays on screen.
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("lumen-render"));
    QGuiApplication::setOrganizationName(lumen::core::organisationName());
    QGuiApplication::setApplicationVersion(lumen::core::version());
    // Closing the operator UI's windows must never be what ends the output.
    QGuiApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lumen Presenter render process"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption serverOption(QStringLiteral("server"),
                                          QStringLiteral("Local socket name to listen on."),
                                          QStringLiteral("name"),
                                          lumen::core::render::defaultServerName());
    const QCommandLineOption screenOption(QStringLiteral("screen"),
                                          QStringLiteral("Screen number for the output (0 is the first)."),
                                          QStringLiteral("index"));
    const QCommandLineOption windowedOption(QStringLiteral("windowed"),
                                            QStringLiteral("Open a window instead of going fullscreen."));
    const QCommandLineOption testModeOption(QStringLiteral("test-mode"),
                                            QStringLiteral("Allow the \"grab\" command (for automated tests)."));
    parser.addOptions({serverOption, screenOption, windowedOption, testModeOption});
    parser.process(app);

    lumen::render::OutputWindow output;
    if (!output.create()) {
        qCritical("lumen-render: could not create the output window");
        return 1;
    }

    lumen::render::RenderServer server(output, parser.isSet(testModeOption));
    QString error;
    if (!server.listen(parser.value(serverOption), &error)) {
        qCritical("lumen-render: %s", qPrintable(error));
        return 2;
    }
    QObject::connect(&server, &lumen::render::RenderServer::quitRequested, &app,
                     &QCoreApplication::quit);

    const QList<QScreen *> screens = QGuiApplication::screens();
    std::optional<int> requested;
    if (parser.isSet(screenOption)) {
        bool ok = false;
        const int index = parser.value(screenOption).toInt(&ok);
        if (ok)
            requested = index;
        if (!ok || index < 0 || index >= screens.size())
            qWarning("lumen-render: screen %s does not exist, choosing one",
                     qPrintable(parser.value(screenOption)));
    }
    auto choice = lumen::render::chooseOutputScreen(
        int(screens.size()), int(screens.indexOf(QGuiApplication::primaryScreen())), requested);
    if (parser.isSet(windowedOption))
        choice.fullscreen = false;
    QScreen *screen = choice.index >= 0 ? screens.at(choice.index) : nullptr;
    output.place(screen, choice.fullscreen);

    qInfo("lumen-render %s listening as %s on screen %d (%s)", qPrintable(lumen::core::version()),
          qPrintable(server.serverName()), choice.index, choice.fullscreen ? "fullscreen" : "windowed");
    return QGuiApplication::exec();
}
