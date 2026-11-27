-- Schema version 5: song usage, for CCLI licence reporting.
--
-- One row per song per day it was shown live, kept on this computer only.
-- The details are copied when shown, so the report stays right if the song
-- is later renamed or deleted. The church exports the report and submits
-- it itself; nothing is sent anywhere.

CREATE TABLE song_usage (
    presentation_id TEXT NOT NULL,
    shown_on        TEXT NOT NULL, -- local date, yyyy-MM-dd
    title           TEXT NOT NULL,
    author          TEXT NOT NULL DEFAULT '',
    ccli            TEXT NOT NULL DEFAULT '',
    copyright       TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (presentation_id, shown_on)
);
CREATE INDEX song_usage_by_day ON song_usage (shown_on);
