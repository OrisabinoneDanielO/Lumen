#pragma once

// Importing songs from other worship software (batch 17): OpenLP and
// EasyWorship 6/7 libraries, and ProPresenter 5 and 6 documents. Each gives
// ParsedSong values that SongLibrary stores like any imported song.
//
// ProPresenter 7 (.pro) documents are in an undocumented binary format and
// are not read; ProPresenter 7 can export lyrics as text, which Lumen
// imports.

#include "lumen/core/result.h"
#include "lumen/core/songs/songimport.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace lumen::core::songs {

// A song read from a library, with where it came from for the report.
struct LibrarySong
{
    QString source; // e.g. "song 12"
    ParsedSong song;
};

enum class LibraryKind { None, OpenLP, EasyWorship };

// What kind of song library a file is (by its tables), if any.
LibraryKind libraryKind(const QString &path);

// OpenLP 2.x/3.x: songs.sqlite (songs, authors and the lyrics XML).
Result<std::vector<LibrarySong>> readOpenLP(const QString &path);

// EasyWorship 6/7: Songs.db with SongWords.db beside it (lyrics as RTF).
Result<std::vector<LibrarySong>> readEasyWorship(const QString &songsDb);

// ProPresenter 5 (.pro5) and 6 (.pro6) XML documents.
Result<ParsedSong> parseProPresenter(const QByteArray &xml, const QString &fallbackTitle);

// Plain text from RTF (paragraphs as lines), as EasyWorship and ProPresenter
// store lyrics: \par and \line break lines; \'hh (Windows-1252) and \uN
// characters are decoded; font tables and other hidden groups are skipped.
QString rtfToText(const QByteArray &rtf);

} // namespace lumen::core::songs
