#pragma once

// Reading what an operator types: "jn 3 16-18", "John 3:16–18", "1 cor 13",
// "Johanu 3:16", "ps 23", "gen 1:1-2:3", "jude 3".

#include "lumen/core/result.h"

#include <QHash>
#include <QSet>
#include <QString>

namespace lumen::core::scripture {

// A passage by book code and numbers. verse 0 means whole chapters.
struct Reference
{
    QString book; // USFM code, e.g. "JHN"
    int chapter = 0;
    int verse = 0;
    int endChapter = 0;
    int endVerse = 0;

    bool wholeChapters() const { return verse == 0; }
    bool operator==(const Reference &) const = default;
};

// Book names and abbreviations people may type, in any language, folded
// (case, accents, spaces) for matching. Starts with English.
class BookNames
{
public:
    BookNames(); // English names and common abbreviations

    // Adds a translation's own name for a book ("Johanu" -> "JHN"). The
    // English names and abbreviations always win, and a name two
    // translations use for different books (one Bible's "Jn" is Jonah) is
    // dropped rather than guessed.
    void add(const QString &name, const QString &code);

    // A translation's name as typed with its accents ("Jọn" is John in Igbo),
    // else an exact name or abbreviation ignoring accents ("jon" is Jonah),
    // else the only book whose full name starts with what was typed (at
    // least 2 letters). Empty if none or ambiguous.
    QString find(const QString &typed) const;

    static QString key(const QString &text); // folded, no spaces or dots

private:
    void addBuiltIn(const QString &key, const QString &code);

    QHash<QString, QString> m_exact;   // key -> code
    QHash<QString, QString> m_names;   // full-name key -> code (for prefixes)
    QSet<QString> m_builtIn;           // English keys: never overridden
    QSet<QString> m_ambiguous;         // keys translations disagree on
    QHash<QString, QString> m_accented; // translation names with accents kept
    QSet<QString> m_accentedAmbiguous;
};

// Parses a reference. Errors explain what was not understood.
Result<Reference> parseReference(const QString &text, const BookNames &names);

// "John 3:16", "John 3:16–18", "Genesis 1:1–2:3", "Psalms 23", "John 3–4".
QString formatReference(const Reference &reference, const QString &bookName);

} // namespace lumen::core::scripture
