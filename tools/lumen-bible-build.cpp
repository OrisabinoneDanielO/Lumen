// Builds bibles.db from the bundled translations (bibles/manifest.json and
// the unpacked USFM packages). Run by the build; see bibles/README.md.
//
//   lumen-bible-build --manifest bibles/manifest.json --source <unpacked> --output bibles.db

#include "lumen/core/scripture/biblestore.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

using namespace lumen::core::scripture;

namespace {

// The package's copyright page (copr.htm) as plain text.
QString noticeFrom(const QString &folder)
{
    QFile file(QDir(folder).filePath(QStringLiteral("copr.htm")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QString html = QString::fromUtf8(file.readAll());
    static const QRegularExpression tags(QStringLiteral("<[^>]+>"));
    html.replace(tags, QStringLiteral(" "));
    html.replace(QStringLiteral("&nbsp;"), QStringLiteral(" ")).replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return html.simplified();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption manifestOption("manifest", "manifest.json", "path");
    const QCommandLineOption sourceOption("source", "Folder of unpacked packages (one per id)", "path");
    const QCommandLineOption outputOption("output", "bibles.db to write", "path");
    parser.addOptions({manifestOption, sourceOption, outputOption});
    parser.process(app);
    QTextStream err(stderr);
    QTextStream out(stdout);

    QFile manifestFile(parser.value(manifestOption));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        err << "Cannot read " << manifestFile.fileName() << "\n";
        return 1;
    }
    const QJsonArray entries = QJsonDocument::fromJson(manifestFile.readAll()).object().value("translations").toArray();

    // Write to a temporary file, then replace the output in one step.
    const QString output = parser.value(outputOption);
    const QString temporary = output + QStringLiteral(".building");
    QFile::remove(temporary);
    {
        auto store = BibleStore::open(temporary, false);
        if (!store.ok()) {
            err << store.error().message << "\n";
            return 1;
        }
        for (const QJsonValue &value : entries) {
            const QJsonObject e = value.toObject();
            const QString id = e.value("id").toString();
            const QString folder = QDir(parser.value(sourceOption)).filePath(id);
            QElapsedTimer timer;
            timer.start();
            auto books = readBibleFiles(folder);
            if (!books.ok()) {
                err << id << ": " << books.error().message << "\n";
                return 1;
            }
            const Translation translation{id,
                                          e.value("abbreviation").toString(),
                                          e.value("title").toString(),
                                          e.value("language").toString(),
                                          e.value("licence").toString(),
                                          e.value("copyright").toString(),
                                          e.value("source").toString(),
                                          noticeFrom(folder)};
            if (auto added = store.value()->add(translation, books.value()); !added.ok()) {
                err << id << ": " << added.error().message << "\n";
                return 1;
            }
            int verses = 0;
            for (const auto &book : books.value())
                verses += book.verseCount();
            out << id << ": " << books.value().size() << " books, " << verses << " verses ("
                << timer.elapsed() << " ms)\n";
        }
    }
    QFile::remove(output);
    if (!QFile::rename(temporary, output)) {
        err << "Cannot write " << output << "\n";
        return 1;
    }
    return 0;
}
