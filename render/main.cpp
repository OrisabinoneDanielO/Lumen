#include "lumen/core/buildinfo.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

// Stub render process. It only reports its version for now; batch 3 turns it
// into the GPU output process that the operator UI talks to over IPC.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("lumen-render"));
    QCoreApplication::setOrganizationName(lumen::core::organisationName());
    QCoreApplication::setApplicationVersion(lumen::core::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Lumen Presenter render process (stub)"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    QTextStream(stdout) << "lumen-render " << lumen::core::version()
                        << ": stub, output rendering arrives in batch 3\n";
    return 0;
}
