#pragma once

#include "lumen/core/result.h"
#include "lumen/core/scripture/bible.h"
#include "lumen/core/scripture/reference.h"

#include <QSqlDatabase>
#include <QString>

#include <memory>
#include <vector>

namespace lumen::core::scripture {

struct Translation
{
    QString id;           // "engwebp"
    QString abbreviation; // button label: "WEB", "Yorùbá"
    QString title;        // "World English Bible"
    QString language;     // "English"
    QString licence;      // "Public domain", "CC BY-SA 4.0"
    QString copyright;    // the line shown with its text
    QString source;       // where it came from
    QString notice;       // the translation's full copyright page, as text
};

struct PassageVerse
{
    QString book;
    int chapter = 0;
    int verse = 0;
    int last = 0;
    QString text;
};

struct Passage
{
    Translation translation;
    Reference reference;   // clamped to what exists
    QString bookName;      // in the translation's language
    QString label;         // "John 3:16–18"
    std::vector<PassageVerse> verses;
};

// One SQLite file of Bible text. The bundled bibles.db is opened read-only;
// Bibles a user adds go into a separate writable store.
class BibleStore
{
public:
    static Result<std::unique_ptr<BibleStore>> open(const QString &path, bool readOnly);
    ~BibleStore();
    BibleStore(const BibleStore &) = delete;
    BibleStore &operator=(const BibleStore &) = delete;

    bool readOnly() const { return m_readOnly; }

    // Stores a translation's books (replacing any with the same id).
    Status add(const Translation &translation, const std::vector<ParsedBook> &books);

    Result<std::vector<Translation>> translations() const;

    // Adds every book name and abbreviation the translations use.
    Status addBookNames(BookNames &names) const;
    // A translation's books in its order: {USFM code, name as it prints it}.
    Result<std::vector<std::pair<QString, QString>>> books(const QString &translationId) const;

    // The verses of a reference, clamped to the chapter's real length. Errors
    // say what does not exist ("John has 21 chapters").
    Result<Passage> passage(const QString &translationId, const Reference &reference) const;

private:
    BibleStore(QString connection, bool readOnly);
    Status createSchema();

    QString m_connection;
    bool m_readOnly;
};

// Imports a translation from USFM files (a folder of .usfm/.sfm files) or a
// USFX .xml file. Notes, headings and markup are removed.
Result<std::vector<ParsedBook>> readBibleFiles(const QString &folderOrFile);

} // namespace lumen::core::scripture
