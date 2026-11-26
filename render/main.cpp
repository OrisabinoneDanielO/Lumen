#include "outputwindow.h"
#include "renderserver.h"
#include "processwatch.h"
#include "screenchoice.h"

#include <QTimer>

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
    const QCommandLineOption stageOption(QStringLiteral("stage"),
                                         QStringLiteral("Be a stage display instead of the audience output."));
    const QCommandLineOption testModeOption(QStringLiteral("test-mode"),
                                            QStringLiteral("Allow the \"grab\" command (for automated tests)."));
    // For tests only: a renderer normally outlives a crashed UI (that is the
    // point of a separate process), but one started by a test that crashed
    // or timed out should not keep playing in the background.
    const QCommandLineOption watchPidOption(QStringLiteral("watch-pid"),
                                            QStringLiteral("Exit when process <pid> has ended (for tests)."),
                                            QStringLiteral("pid"));
    parser.addOptions({serverOption, screenOption, windowedOption, stageOption, testModeOption, watchPidOption});
    parser.process(app);

    QTimer watchdog;
    if (parser.isSet(watchPidOption)) {
        const qint64 pid = parser.value(watchPidOption).toLongLong();
        QObject::connect(&watchdog, &QTimer::timeout, &app, [pid] {
            if (!lumen::render::processRunning(pid)) {
                qInfo("lumen-render: process %lld ended; exiting", pid);
                QCoreApplication::quit();
            }
        });
        watchdog.start(1000);
    }

    lumen::render::OutputWindow output(parser.isSet(stageOption) ? lumen::render::OutputWindow::Kind::Stage
                                                                  : lumen::render::OutputWindow::Kind::Audience);
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
