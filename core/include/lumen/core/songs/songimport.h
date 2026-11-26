#pragma once

// Reading songs from files: plain text and OpenLyrics, plus the shared rules
// for section labels, splitting sections into slides, and search text.

#include "lumen/core/result.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <vector>

namespace lumen::core::songs {

struct LyricLine
{
    QString text;
    QString chords; // chord names on this line, e.g. "G C D" (kept for later)

    bool operator==(const LyricLine &) const = default;
};

// One section of a song ("Verse 1", "Chorus"). Its lines come in blocks: the
// operator's explicit slide breaks ("---" in text, separate <lines> in
// OpenLyrics). Each block is then split into slides by line count.
struct ParsedSection
{
    QString label;
    std::vector<std::vector<LyricLine>> blocks;

    int lineCount() const;
    QStringList texts() const; // every line's text, in order
    bool operator==(const ParsedSection &) const = default;
};

struct ParsedSong
{
    QString title;
    QStringList authors;
    QString copyright;
    QString ccli;
    QString key;
    QString tempo;
    std::vector<ParsedSection> sections; // each label once, in first-seen order
    QStringList order;                   // labels in play order (repeats allowed)
    QStringList warnings;                // things the importer could not use
};

// Bytes to text: UTF-8 (with or without BOM), UTF-16 (BOM), otherwise
// Windows-1252, the usual encoding of older Windows tools.
QString decodeText(const QByteArray &bytes);

// Windows-1252 bytes to text. Written out rather than through QStringDecoder,
// which only knows the codepage where Qt was built with ICU (not on macOS
// or Windows).
QString decodeWindows1252(const QByteArray &bytes);

// "my_song-title.txt" -> "My Song Title".
QString titleFromFileName(const QString &fileName);

// A section header in any common spelling, as Lumen's canonical label:
// "V1:", "[verse 1]", "VERSE 1" -> "Verse 1"; "c", "Refrain" -> "Chorus";
// "PC", "Pre-Chorus" -> "Pre-chorus"; "B" -> "Bridge"; "End", "Outro" ->
// "Ending". Returns an empty string if the line is not a header.
QString sectionLabelFromHeader(const QString &line);

// Lyrics where a blank line inside a section starts a new slide (the slide
// editor's rule, and EasyWorship's): turns those blank lines into the
// explicit slide breaks ("---") plain-text import understands. A blank line
// before a section header still ends the section.
QString slideBreaksFromBlankLines(const QString &text);

// Plain-text lyrics. See songimport.cpp for the accepted layouts.
Result<ParsedSong> parseTextSong(const QString &text, const QString &fallbackTitle);

// OpenLyrics 0.8/0.9 XML (http://openlyrics.info).
Result<ParsedSong> parseOpenLyrics(const QByteArray &xml);

// Splits lines into slides of at most maxLines, as evenly as possible:
// 5 lines at 2 per slide -> 2 + 2 + 1; 6 at 4 -> 3 + 3.
std::vector<std::vector<LyricLine>> splitIntoSlides(const std::vector<LyricLine> &lines, int maxLines);

// Text as stored in the search index: case-folded, accents and tone marks
// removed (Yoruba ẹ -> e), Hausa hooked letters plain (ɗ -> d), punctuation
// as single spaces.
QString normaliseForSearch(const QString &text);

} // namespace lumen::core::songs
