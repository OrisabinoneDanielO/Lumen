#pragma once

#include "lumen/core/data/database.h"
#include "lumen/core/result.h"
#include "lumen/core/songs/songimport.h"

#include <QStringList>

#include <vector>

namespace lumen::core::songs {

struct ImportedSong
{
    QString presentationId;
    QString title;
    bool duplicate = false; // already in the library; nothing was added
};

struct ImportReport
{
    int imported = 0;
    int skipped = 0; // duplicates
    int failed = 0;
    QStringList messages;        // one line per skipped or failed file
    QStringList presentationIds; // songs added

    // "18 imported, 1 skipped, 1 failed"
    QString summary() const;
};

struct SongHit
{
    QString presentationId;
    QString title;
    QString author;
    QString copyright;
    QString key;
};

// The songs in the library: importing, the search index and searching.
class SongLibrary
{
public:
    // linesPerSlide: the auto-split limit (2 by default, for readability at
    // the back of the room).
    explicit SongLibrary(data::Database &db, int linesPerSlide = 2);

    int linesPerSlide() const { return m_linesPerSlide; }
    void setLinesPerSlide(int lines) { m_linesPerSlide = qMax(1, lines); }

    // Reads a .txt or OpenLyrics .xml file.
    Result<ParsedSong> parseFile(const QString &path) const;

    // Stores a song: groups, slides split by line count, text elements, the
    // default arrangement and its search entry, all or nothing. A song with
    // the same title and first line as one in the library is not added again.
    Result<ImportedSong> add(const ParsedSong &song, const QString &libraryId = {});

    // Imports files and folders (folders: every .txt and .xml inside).
    ImportReport importFiles(const QStringList &paths);

    // Songs whose title, author, CCLI number or lyrics contain words starting
    // with every word of the query, ignoring case and accents. Title matches
    // first. An empty query lists every song by title.
    Result<std::vector<SongHit>> search(const QString &query, int limit = 500) const;

    // Rebuilds one song's search entry (after edits made elsewhere).
    Status reindex(const QString &presentationId);
    // Adds search entries for songs that have none (e.g. after upgrading).
    Status indexMissing();

    // The library new songs go into: the first one, or a new "Main library".
    Result<QString> defaultLibraryId();

private:
    data::Database &m_db;
    int m_linesPerSlide;
};

} // namespace lumen::core::songs
