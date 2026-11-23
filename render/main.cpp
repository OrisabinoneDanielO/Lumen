#include "outputwindow.h"
#include "renderserver.h"
#include "keyfill.h"
#include "ndisink.h"
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
    // The stream output (batch 15): Slide and Props on transparency, drawn
    // off screen, sent as key and fill windows and/or NDI.
    const QCommandLineOption streamOption(QStringLiteral("stream"),
                                          QStringLiteral("Be the stream output (Slide and Props on transparency)."));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Stream picture size (default 1920x1080)."),
                                        QStringLiteral("WxH"));
    const QCommandLineOption fpsOption(QStringLiteral("fps"), QStringLiteral("Stream frames per second (default 30)."),
                                       QStringLiteral("n"));
    const QCommandLineOption keyFillOption(QStringLiteral("key-fill"),
                                           QStringLiteral("Show the stream as key and fill (windows, or screens below)."));
    const QCommandLineOption fillScreenOption(QStringLiteral("fill-screen"), QStringLiteral("Screen for the fill."),
                                              QStringLiteral("index"));
    const QCommandLineOption keyScreenOption(QStringLiteral("key-screen"), QStringLiteral("Screen for the key."),
                                             QStringLiteral("index"));
    const QCommandLineOption ndiOption(QStringLiteral("ndi"),
                                       QStringLiteral("Send the stream over NDI as <name> (needs the NDI Runtime; experimental)."),
                                       QStringLiteral("name"));
    const QCommandLineOption testModeOption(QStringLiteral("test-mode"),
                                            QStringLiteral("Allow the \"grab\" command (for automated tests)."));
    // For tests only: a renderer normally outlives a crashed UI (that is the
    // point of a separate process), but one started by a test that crashed
    // or timed out should not keep playing in the background.
    const QCommandLineOption watchPidOption(QStringLiteral("watch-pid"),
                                            QStringLiteral("Exit when process <pid> has ended (for tests)."),
                                            QStringLiteral("pid"));
    parser.addOptions({serverOption, screenOption, windowedOption, stageOption, streamOption, sizeOption, fpsOption,
                       keyFillOption, fillScreenOption, keyScreenOption, ndiOption, testModeOption, watchPidOption});
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

    using Kind = lumen::render::OutputWindow::Kind;
    lumen::render::OutputWindow output(parser.isSet(streamOption) ? Kind::Stream
                                       : parser.isSet(stageOption) ? Kind::Stage
                                                                   : Kind::Audience);
    if (parser.isSet(streamOption)) {
        const QStringList size = parser.value(sizeOption).split(u'x');
        output.setStreamFormat(size.size() == 2 ? QSize(size[0].toInt(), size[1].toInt()) : QSize(),
                               parser.value(fpsOption).toInt());
    }
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

    // Stream outputs have no window of their own: their frames go to key
    // and fill windows and/or NDI.
    std::unique_ptr<lumen::render::PictureWindow> fill, key;
    std::unique_ptr<lumen::render::NdiSink> ndi;
    if (output.kind() == Kind::Stream) {
        QStringList status;
        if (parser.isSet(keyFillOption)) {
            const auto open = [&](const QString &title, const QCommandLineOption &screenOption, int offset) {
                auto window = std::make_unique<lumen::render::PictureWindow>();
                window->setTitle(title);
                bool ok = false;
                const int index = parser.value(screenOption).toInt(&ok);
                if (ok && index >= 0 && index < screens.size()) {
                    window->setScreen(screens.at(index));
                    window->setGeometry(screens.at(index)->geometry());
                    window->showFullScreen();
                } else {
                    window->resize(640, 360);
                    window->setPosition(40 + offset, 40);
                    window->show();
                }
                return window;
            };
            fill = open(QStringLiteral("Lumen Fill"), fillScreenOption, 0);
            key = open(QStringLiteral("Lumen Key"), keyScreenOption, 680);
            status << QStringLiteral("key and fill");
        }
        if (parser.isSet(ndiOption)) {
            ndi = std::make_unique<lumen::render::NdiSink>(parser.value(ndiOption), parser.value(fpsOption).toInt() > 0
                                                                                        ? parser.value(fpsOption).toInt() : 30);
            status << QStringLiteral("NDI: %1").arg(ndi->status());
            if (!ndi->isSending())
                qWarning("lumen-render: %s", qPrintable(ndi->status()));
        }
        output.setStreamStatus(status.isEmpty() ? QStringLiteral("no sinks") : status.join(QStringLiteral("; ")));
        QObject::connect(&output, &lumen::render::OutputWindow::streamFrame, &app, [&](const QImage &frame) {
            if (fill)
                fill->setPicture(lumen::render::fillOf(frame));
            if (key)
                key->setPicture(lumen::render::keyOf(frame));
            if (ndi)
                ndi->frame(frame);
        });
        qInfo("lumen-render %s: stream output (%s) listening as %s", qPrintable(lumen::core::version()),
              qPrintable(output.streamStatus()), qPrintable(server.serverName()));
        return QGuiApplication::exec();
    }

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
