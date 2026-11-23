#include "deckimporter.h"

#include "pptxreader.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <functional>
#include <set>

namespace lumen::app {

namespace {

const QStringList pdfSuffixes{"pdf"};
const QStringList officeSuffixes{"pptx", "ppt", "pps", "ppsx", "odp"};

QSize fit(QSizeF size, const DeckImporter::Options &options)
{
    if (size.isEmpty())
        return {options.width, options.height};
    size.scale(options.width, options.height, Qt::KeepAspectRatio);
    return size.toSize().expandedTo(QSize(1, 1));
}

// Saves slide pictures and thumbnails into `folder` as slide-001.png and
// thumb-001.jpg. `count` pictures come from `picture(i)`; `keep(i)` says
// which to use.
bool savePictures(DeckImporter::Result &result, const QString &folder, int count,
                  const std::function<QImage(int)> &picture, const std::function<bool(int)> &keep)
{
    QDir().mkpath(folder);
    for (int i = 0; i < count; ++i) {
        if (!keep(i))
            continue;
        QImage image = picture(i);
        if (image.isNull()) {
            result.error = QStringLiteral("slide %1 could not be drawn").arg(i + 1);
            return false;
        }
        const int n = int(result.slides.size()) + 1;
        const QString slide = QDir(folder).filePath(QStringLiteral("slide-%1.png").arg(n, 3, 10, QLatin1Char('0')));
        const QString thumb = QDir(folder).filePath(QStringLiteral("thumb-%1.jpg").arg(n, 3, 10, QLatin1Char('0')));
        if (!image.save(slide, "PNG")
            || !image.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(thumb, "JPG", 85)) {
            result.error = QStringLiteral("the slide pictures could not be saved in %1").arg(folder);
            return false;
        }
        result.slides << slide;
        result.thumbnails << thumb;
    }
    return true;
}

// Draws every page of a PDF; `keep(i)` picks pages.
bool importPdf(DeckImporter::Result &result, const QString &pdf, const QString &folder,
               const DeckImporter::Options &options, const std::function<bool(int)> &keep, int *pageCount = nullptr)
{
    QPdfDocument doc;
    if (doc.load(pdf) != QPdfDocument::Error::None || doc.pageCount() <= 0) {
        result.error = QStringLiteral("the PDF could not be read");
        return false;
    }
    if (pageCount) {
        *pageCount = doc.pageCount();
        return true;
    }
    return savePictures(result, folder, doc.pageCount(), [&](int i) {
        const QSize size = fit(doc.pagePointSize(i), options);
        const QImage page = doc.render(i, size);
        if (page.isNull())
            return QImage();
        // Pages are transparent where nothing is drawn; slides are not.
        QImage slide(size, QImage::Format_RGB32);
        slide.fill(Qt::white);
        QPainter painter(&slide);
        painter.drawImage(0, 0, page);
        return slide;
    }, keep);
}

QString officePdf(const QString &program, const QStringList &arguments, const QString &pdf, int timeoutMs)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(10000))
        return {};
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(5000);
        return {};
    }
    return process.exitStatus() == QProcess::NormalExit && QFileInfo::exists(pdf) ? pdf : QString();
}

QString convertWithLibreOffice(const QString &soffice, const QString &path, const QString &outDir, int timeoutMs)
{
    // A profile of its own, so a LibreOffice the user has open is untouched.
    QTemporaryDir profile;
    const QString pdf = QDir(outDir).filePath(QFileInfo(path).completeBaseName() + QStringLiteral(".pdf"));
    return officePdf(soffice,
                     {QStringLiteral("--headless"), QStringLiteral("--norestore"), QStringLiteral("--nologo"),
                      QStringLiteral("--nodefault"),
                      QStringLiteral("-env:UserInstallation=") + QUrl::fromLocalFile(profile.path()).toString(),
                      QStringLiteral("--convert-to"), QStringLiteral("pdf"), QStringLiteral("--outdir"), outDir, path},
                     pdf, timeoutMs);
}

QString convertWithPowerPoint(const QString &path, const QString &outDir, int timeoutMs)
{
    const QString pdf = QDir(outDir).filePath(QStringLiteral("deck.pdf"));
    const auto quoted = [](QString s) { return QStringLiteral("'") + s.replace(u'\'', QStringLiteral("''")) + u'\''; };
    // Read-only, no window; 32 is ppSaveAsPDF.
    const QString script =
        QStringLiteral("$ErrorActionPreference='Stop'; $pp = New-Object -ComObject PowerPoint.Application; "
                       "try { $p = $pp.Presentations.Open(%1, $true, $false, $false); $p.SaveAs(%2, 32); $p.Close() } "
                       "finally { $pp.Quit() }")
            .arg(quoted(QDir::toNativeSeparators(path)), quoted(QDir::toNativeSeparators(pdf)));
    return officePdf(QStringLiteral("powershell"),
                     {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), script},
                     pdf, timeoutMs);
}

} // namespace

bool DeckImporter::canImport(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return pdfSuffixes.contains(suffix) || officeSuffixes.contains(suffix);
}

QString DeckImporter::findLibreOffice()
{
    const QString chosen = qEnvironmentVariable("LUMEN_SOFFICE");
    if (chosen == QLatin1String("none"))
        return {};
    if (!chosen.isEmpty())
        return QFileInfo(chosen).isExecutable() ? chosen : QString();
    for (const char *name : {"soffice", "libreoffice"}) {
        const QString found = QStandardPaths::findExecutable(QLatin1String(name));
        if (!found.isEmpty())
            return found;
    }
    const QStringList known{
        QStringLiteral("C:/Program Files/LibreOffice/program/soffice.exe"),
        QStringLiteral("C:/Program Files (x86)/LibreOffice/program/soffice.exe"),
        QStringLiteral("/Applications/LibreOffice.app/Contents/MacOS/soffice"),
        QStringLiteral("/opt/libreoffice/program/soffice"),
    };
    for (const QString &candidate : known) {
        if (QFileInfo(candidate).isExecutable())
            return candidate;
    }
    return {};
}

bool DeckImporter::hasPowerPoint()
{
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("LUMEN_POWERPOINT") == QLatin1String("none"))
        return false;
    QSettings key(QStringLiteral("HKEY_CLASSES_ROOT\\PowerPoint.Application"), QSettings::NativeFormat);
    return key.childGroups().contains(QStringLiteral("CLSID"), Qt::CaseInsensitive);
#else
    return false;
#endif
}

DeckImporter::Result DeckImporter::import(const QString &path, const QString &outputFolder, const Options &options)
{
    Result result;
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("key")) {
        result.error = QStringLiteral("Keynote files cannot be read directly: in Keynote choose File > Export To > PDF, then import the PDF");
        return result;
    }
    if (!info.isFile() || !canImport(path)) {
        result.error = QStringLiteral("not a presentation Lumen can import (PowerPoint, OpenDocument or PDF)");
        return result;
    }
    const auto all = [](int) { return true; };

    if (pdfSuffixes.contains(suffix)) {
        result.method = QStringLiteral("pdf");
        importPdf(result, path, outputFolder, options, all);
        return result;
    }

    // PowerPoint: read the slide list first (when it is a .pptx), so the
    // converted PDF can be checked against it and hidden slides left out.
    PptxReader reader;
    const bool readable = suffix == QLatin1String("pptx") || suffix == QLatin1String("ppsx")
                              ? reader.open(path) : false;
    if (!readable && (suffix == QLatin1String("pptx") || suffix == QLatin1String("ppsx"))) {
        result.error = reader.error();
        return result;
    }
    const int total = readable ? int(reader.slides().size()) : -1;
    const int visible = readable ? reader.visibleCount() : -1;
    result.hiddenSkipped = readable ? total - visible : 0;

    if (options.office == Office::Auto) {
        QTemporaryDir work;
        QString pdf;
        QString method;
        if (hasPowerPoint()) {
            pdf = convertWithPowerPoint(info.absoluteFilePath(), work.path(), options.officeTimeoutMs);
            method = QStringLiteral("powerpoint");
        }
        const QString soffice = pdf.isEmpty() ? findLibreOffice() : QString();
        if (pdf.isEmpty() && !soffice.isEmpty()) {
            pdf = convertWithLibreOffice(soffice, info.absoluteFilePath(), work.path(), options.officeTimeoutMs);
            method = QStringLiteral("libreoffice");
        }
        if (!pdf.isEmpty()) {
            int pages = 0;
            Result probe;
            importPdf(probe, pdf, outputFolder, options, all, &pages);
            // Office programs leave hidden slides out of the PDF; if one kept
            // them, drop those pages here.
            std::function<bool(int)> keep = all;
            bool matches = !readable || pages == visible;
            if (readable && !matches && pages == total) {
                keep = [&reader](int i) { return !reader.slides()[size_t(i)].hidden; };
                matches = true;
            }
            if (matches) {
                result.method = method;
                importPdf(result, pdf, outputFolder, options, keep);
                if (result.ok() || !readable)
                    return result;
            }
            result = Result{};
            result.hiddenSkipped = total - visible;
            result.note = QStringLiteral("%1 gave %2 pages for %3 slides; Lumen drew the slides itself")
                              .arg(method == QLatin1String("powerpoint") ? QStringLiteral("PowerPoint") : QStringLiteral("LibreOffice"))
                              .arg(pages).arg(visible);
        } else if (!readable) {
            result.error = QStringLiteral("needs PowerPoint or LibreOffice to import (or save it as .pptx or PDF)");
            return result;
        } else {
            result.note = QStringLiteral("drawn by Lumen (install LibreOffice for slides that look exactly as designed)");
        }
    } else if (!readable) {
        result.error = QStringLiteral("needs PowerPoint or LibreOffice to import (or save it as .pptx or PDF)");
        return result;
    }

    // Lumen's own basic drawing.
    result.method = QStringLiteral("basic");
    const QSize size = fit(reader.slideSize(), options);
    savePictures(result, outputFolder, total, [&](int i) { return reader.render(i, size.width()); },
                 [&reader](int i) { return !reader.slides()[size_t(i)].hidden; });
    return result;
}

} // namespace lumen::app
