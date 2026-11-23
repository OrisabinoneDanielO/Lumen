-- Schema version 2: the song search index (batch 6).
--
-- One row per song holding searchable text that has already been normalised
-- (case-folded, accents and tone marks removed, punctuation turned into
-- spaces) by lumen::core::songs::normaliseForSearch, so a plain LIKE matches
-- the starts of words on every SQLite build. The song library keeps it up to
-- date; rows disappear with their presentation.

CREATE TABLE song_index (
    presentation_id TEXT PRIMARY KEY NOT NULL REFERENCES presentations (id) ON DELETE CASCADE,
    title           TEXT NOT NULL DEFAULT '',
    author          TEXT NOT NULL DEFAULT '',
    ccli            TEXT NOT NULL DEFAULT '',
    lyrics          TEXT NOT NULL DEFAULT '',
    first_line      TEXT NOT NULL DEFAULT ''   -- with title, spots duplicates on import
);
CREATE INDEX song_index_title ON song_index (title, first_line);
