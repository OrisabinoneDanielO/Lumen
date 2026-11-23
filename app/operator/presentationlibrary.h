#pragma once

#include "deckimporter.h"

#include "lumen/core/data/database.h"
#include "lumen/core/result.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace lumen::app {

struct DeckRow
{
    QString presentationId;
    QString title;
    int slides = 0;
    QString thumbnail; // the first slide's
    QString method;    // how it was imported: "pdf", "libreoffice", "powerpoint", "basic"
};

struct DeckImportReport
{
    int imported = 0;
    int skipped = 0;
    int failed = 0;
    QStringList messages;
    QString summary() const;
};

// Imported slide decks (PowerPoint, PDF). Each becomes a Slides
// presentation with one picture per slide, stored as files in a folder of
// its own under `slideFolder`. Rendering the pictures (render()) touches no
// database and can run on another thread; store() then adds the result to
// the library on the database's thread.
class PresentationLibrary
{
public:
    PresentationLibrary(core::data::Database &db, QString slideFolder);

    struct Rendered
    {
        QString path;
        QString hash;
        DeckImporter::Result result;
        bool duplicate = false;
    };

    // Fingerprints of the decks already in the library (database thread).
    QStringList knownHashes() const;

    // Draws each file's slides into slideFolder, skipping files whose
    // fingerprint is in `known`. Uses no database: safe on any thread.
    static std::vector<Rendered> render(const QStringList &paths, const QStringList &known, const QString &slideFolder,
                                        const DeckImporter::Options &options = {});
    DeckImportReport store(const std::vector<Rendered> &rendered);

    // All at once (for tests and small imports).
    DeckImportReport importFiles(const QStringList &paths, const DeckImporter::Options &options = {})
    {
        return store(render(paths, knownHashes(), m_slides, options));
    }

    core::Result<std::vector<DeckRow>> list() const;
    QString slideFolder() const { return m_slides; }

private:
    core::data::Database &m_db;
    QString m_slides;
};

} // namespace lumen::app
