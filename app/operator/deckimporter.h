#pragma once

#include <QString>
#include <QStringList>

namespace lumen::app {

// Turns a presentation file into one picture per slide, in show order.
//
// - PDF (and Keynote exported as PDF): each page, with Qt PDF.
// - PowerPoint (.pptx), and .ppt/.odp: converted to PDF by an installed
//   office program (PowerPoint on Windows, else LibreOffice) so the slides
//   look exactly as designed. If none is installed, or its result has the
//   wrong number of slides, a .pptx is drawn by Lumen's own basic reader
//   (PptxReader) and the result says the slides may look different.
// - Hidden slides are left out, as in a slide show.
//
// Thread-safe and independent of the library database, so it can run off
// the UI thread.
class DeckImporter
{
public:
    enum class Office { Auto, Never };

    struct Options
    {
        Office office = Office::Auto;
        int width = 1920;              // slide pictures fit in width x height
        int height = 1080;
        int officeTimeoutMs = 180000;
    };

    struct Result
    {
        QStringList slides;      // picture files, in order
        QStringList thumbnails;  // 320 x 180 JPEGs, same order
        QString method;          // "pdf", "libreoffice", "powerpoint" or "basic"
        int hiddenSkipped = 0;
        QString note;            // for the import report, e.g. a fallback
        QString error;           // set when nothing could be imported
        bool ok() const { return error.isEmpty() && !slides.isEmpty(); }
    };

    static bool canImport(const QString &path);
    static Result import(const QString &path, const QString &outputFolder, const Options &options);
    static Result import(const QString &path, const QString &outputFolder) { return import(path, outputFolder, Options{}); }

    // The installed LibreOffice (soffice) or empty. LUMEN_SOFFICE overrides
    // the search: a path, or "none" to act as if it is not installed.
    static QString findLibreOffice();
    static bool hasPowerPoint();
};

} // namespace lumen::app
