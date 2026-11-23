#pragma once

// Bible text: the canonical books, and reading USFM and USFX files.

#include "lumen/core/result.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <map>
#include <vector>

namespace lumen::core::scripture {

// One of the 66 books, by its USFM code ("GEN" ... "REV").
struct BookInfo
{
    const char *code;
    const char *englishName;
    std::vector<const char *> abbreviations; // lower case, no spaces or dots
    bool singleChapter = false;
};

// The 66 books in canonical order.
const std::vector<BookInfo> &canonicalBooks();
// 1-based position in the canon; 100+ for other books (deuterocanon etc.).
int bookOrdinal(const QString &code);
const BookInfo *bookInfo(const QString &code);

struct Verse
{
    int number = 0;
    int last = 0;  // the last verse it covers ("16-17" is 16 to 17)
    QString text;  // plain text: notes, cross-references and markup removed

    bool operator==(const Verse &) const = default;
};

struct ParsedBook
{
    QString code;         // "JHN"
    QString name;         // as the translation names it, e.g. "Johanu"
    QString longName;     // "Ìwé Ìyìnrere Johanu"
    QString abbreviation; // "Jh"
    std::map<int, std::vector<Verse>> chapters;

    int verseCount() const;
};

// Parses one USFM book file. Keeps verse text (including words of Jesus and
// added words); drops footnotes, cross-references, headings, titles and
// word-level attributes such as Strong's numbers.
Result<ParsedBook> parseUsfmBook(const QString &usfm);

// Parses a USFX file (one XML file holding every book).
Result<std::vector<ParsedBook>> parseUsfx(const QByteArray &xml);

} // namespace lumen::core::scripture
