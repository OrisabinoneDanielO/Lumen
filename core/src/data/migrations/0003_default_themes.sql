-- Schema version 3: one default theme per kind of item (batch 8).
--
-- The default theme for songs, scripture, lower thirds, ... is used by any
-- presentation of that kind that has no theme of its own.

ALTER TABLE themes ADD COLUMN is_default INTEGER NOT NULL DEFAULT 0 CHECK (is_default IN (0, 1));
CREATE UNIQUE INDEX themes_one_default ON themes (applies_to) WHERE is_default = 1;
