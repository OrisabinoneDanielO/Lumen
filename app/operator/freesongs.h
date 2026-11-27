#pragma once

#include "lumen/core/data/database.h"
#include "lumen/core/result.h"
#include "lumen/core/songs/songlibrary.h"

#include <QByteArray>

namespace lumen::app {

// The free songs every library starts with (songs/free/songs.json, built
// into Lumen): public-domain and Creative Commons hymns and songs with their
// chords, from WorshipCommons and the OpenLP song collection. See
// songs/README.md for where they come from and their licences.
//
// Adds the ones the library does not have yet (by title and first line, as
// any import), so it can run on every start-up of a new version; returns how
// many were added.
// All in one transaction: one write to disk, not one per song.
core::Result<int> addFreeSongs(core::data::Database &db, core::songs::SongLibrary &songs, const QByteArray &collection);
// The collection built into this Lumen, and its version.
QByteArray builtInFreeSongs();
int builtInFreeSongsVersion();

} // namespace lumen::app
