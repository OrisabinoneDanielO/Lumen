-- Schema version 1: the Data model in docs/design-doc.md.
--
-- Ids are UUID strings. JSON columns hold settings whose shape changes often.
-- Ownership cascades on delete; optional references are set to NULL; a
-- presentation used by a plan, or media used by an element, cannot be deleted
-- (NO ACTION, checked at the end of the statement so cascades still work).
--
-- The migration runner splits this file on semicolons outside quotes and
-- comments, so triggers (which contain semicolons) are not supported here.

CREATE TABLE libraries (
    id          TEXT PRIMARY KEY NOT NULL,
    name        TEXT NOT NULL,
    folder_path TEXT NOT NULL DEFAULT '',
    last_backup TEXT                                   -- ISO 8601 UTC
);

CREATE TABLE themes (
    id         TEXT PRIMARY KEY NOT NULL,
    name       TEXT NOT NULL,
    applies_to TEXT NOT NULL DEFAULT 'any'
               CHECK (applies_to IN ('any', 'song', 'scripture', 'slides', 'media', 'lower_third')),
    background TEXT NOT NULL DEFAULT '{}',
    safe_area  TEXT NOT NULL DEFAULT '{}'
);

CREATE TABLE presentations (
    id         TEXT PRIMARY KEY NOT NULL,
    library_id TEXT NOT NULL REFERENCES libraries (id) ON DELETE CASCADE,
    kind       TEXT NOT NULL CHECK (kind IN ('song', 'scripture', 'slides', 'media')),
    title      TEXT NOT NULL,
    theme_id   TEXT REFERENCES themes (id) ON DELETE SET NULL,
    metadata   TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX presentations_library ON presentations (library_id);
CREATE INDEX presentations_theme ON presentations (theme_id);

-- "groups" is an SQL keyword, hence slide_groups.
CREATE TABLE slide_groups (
    id              TEXT PRIMARY KEY NOT NULL,
    presentation_id TEXT NOT NULL REFERENCES presentations (id) ON DELETE CASCADE,
    position        INTEGER NOT NULL DEFAULT 0,
    label           TEXT NOT NULL DEFAULT '',
    colour          TEXT NOT NULL DEFAULT '',
    hotkey          TEXT NOT NULL DEFAULT ''
);
CREATE INDEX slide_groups_presentation ON slide_groups (presentation_id, position);

CREATE TABLE arrangements (
    id              TEXT PRIMARY KEY NOT NULL,
    presentation_id TEXT NOT NULL REFERENCES presentations (id) ON DELETE CASCADE,
    name            TEXT NOT NULL DEFAULT '',
    is_default      INTEGER NOT NULL DEFAULT 0 CHECK (is_default IN (0, 1))
);
CREATE UNIQUE INDEX arrangements_one_default ON arrangements (presentation_id) WHERE is_default = 1;

-- Play order of an arrangement; a group may appear many times.
CREATE TABLE arrangement_groups (
    arrangement_id TEXT NOT NULL REFERENCES arrangements (id) ON DELETE CASCADE,
    position       INTEGER NOT NULL,
    group_id       TEXT NOT NULL REFERENCES slide_groups (id) ON DELETE CASCADE,
    PRIMARY KEY (arrangement_id, position)
);
CREATE INDEX arrangement_groups_group ON arrangement_groups (group_id);

CREATE TABLE slides (
    id         TEXT PRIMARY KEY NOT NULL,
    group_id   TEXT NOT NULL REFERENCES slide_groups (id) ON DELETE CASCADE,
    position   INTEGER NOT NULL DEFAULT 0,
    notes      TEXT NOT NULL DEFAULT '',
    chords     TEXT NOT NULL DEFAULT '',
    transition TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX slides_group ON slides (group_id, position);

CREATE TABLE media_assets (
    id             TEXT PRIMARY KEY NOT NULL,
    library_id     TEXT NOT NULL REFERENCES libraries (id) ON DELETE CASCADE,
    file_path      TEXT NOT NULL,
    hash           TEXT NOT NULL DEFAULT '',
    duration_ms    INTEGER NOT NULL DEFAULT 0,
    thumbnail_path TEXT NOT NULL DEFAULT '',
    loop           INTEGER NOT NULL DEFAULT 0 CHECK (loop IN (0, 1)),
    trim_in_ms     INTEGER NOT NULL DEFAULT 0,
    trim_out_ms    INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX media_assets_library ON media_assets (library_id);
CREATE INDEX media_assets_hash ON media_assets (hash);

CREATE TABLE elements (
    id             TEXT PRIMARY KEY NOT NULL,
    slide_id       TEXT REFERENCES slides (id) ON DELETE CASCADE,
    theme_id       TEXT REFERENCES themes (id) ON DELETE CASCADE,
    type           TEXT NOT NULL CHECK (type IN ('text', 'image', 'video', 'shape', 'timer', 'clock')),
    x              REAL NOT NULL DEFAULT 0,
    y              REAL NOT NULL DEFAULT 0,
    width          REAL NOT NULL DEFAULT 0,
    height         REAL NOT NULL DEFAULT 0,
    style          TEXT NOT NULL DEFAULT '{}',
    z_order        INTEGER NOT NULL DEFAULT 0,
    content        TEXT NOT NULL DEFAULT '',
    media_asset_id TEXT REFERENCES media_assets (id),
    CHECK ((slide_id IS NULL) <> (theme_id IS NULL))
);
CREATE INDEX elements_slide ON elements (slide_id, z_order);
CREATE INDEX elements_theme ON elements (theme_id, z_order);
CREATE INDEX elements_media ON elements (media_asset_id);

CREATE TABLE plans (
    id   TEXT PRIMARY KEY NOT NULL,
    name TEXT NOT NULL,
    date TEXT                                           -- ISO 8601 date
);

CREATE TABLE plan_items (
    id              TEXT PRIMARY KEY NOT NULL,
    plan_id         TEXT NOT NULL REFERENCES plans (id) ON DELETE CASCADE,
    position        INTEGER NOT NULL DEFAULT 0,
    presentation_id TEXT REFERENCES presentations (id),
    arrangement_id  TEXT REFERENCES arrangements (id) ON DELETE SET NULL,
    header_text     TEXT NOT NULL DEFAULT '',
    notes           TEXT NOT NULL DEFAULT '',
    CHECK (presentation_id IS NOT NULL OR header_text <> '')
);
CREATE INDEX plan_items_plan ON plan_items (plan_id, position);
CREATE INDEX plan_items_presentation ON plan_items (presentation_id);
CREATE INDEX plan_items_arrangement ON plan_items (arrangement_id);

CREATE TABLE stage_layouts (
    id      TEXT PRIMARY KEY NOT NULL,
    name    TEXT NOT NULL,
    widgets TEXT NOT NULL DEFAULT '[]'
);

CREATE TABLE outputs (
    id              TEXT PRIMARY KEY NOT NULL,
    name            TEXT NOT NULL,
    target          TEXT NOT NULL DEFAULT '',
    width           INTEGER NOT NULL DEFAULT 1920 CHECK (width > 0),
    height          INTEGER NOT NULL DEFAULT 1080 CHECK (height > 0),
    frame_rate      REAL NOT NULL DEFAULT 60 CHECK (frame_rate > 0),
    layout          TEXT NOT NULL DEFAULT 'audience' CHECK (layout IN ('audience', 'stage', 'key_fill')),
    layers          TEXT NOT NULL DEFAULT '{}',
    stage_layout_id TEXT REFERENCES stage_layouts (id) ON DELETE SET NULL
);
CREATE INDEX outputs_stage_layout ON outputs (stage_layout_id);

CREATE TABLE messages (
    id            TEXT PRIMARY KEY NOT NULL,
    name          TEXT NOT NULL,
    template_text TEXT NOT NULL DEFAULT '',
    fields        TEXT NOT NULL DEFAULT '[]'
);

CREATE TABLE timers (
    id          TEXT PRIMARY KEY NOT NULL,
    name        TEXT NOT NULL,
    mode        TEXT NOT NULL DEFAULT 'countdown' CHECK (mode IN ('countdown', 'countdown_to_time', 'elapsed')),
    duration_ms INTEGER NOT NULL DEFAULT 0 CHECK (duration_ms >= 0),
    target_time TEXT                                   -- local time of day, HH:mm:ss.zzz
);
