-- Schema version 4: props (batch 14).
--
-- Overlays that stay up across slides: lower thirds (a name and a title)
-- and bugs (a logo in a corner). data holds the kind's settings as JSON:
-- lower thirds {"line1", "line2"}, bugs {"image", "corner", "size"}.

CREATE TABLE props (
    id   TEXT PRIMARY KEY NOT NULL,
    name TEXT NOT NULL,
    kind TEXT NOT NULL CHECK (kind IN ('lower_third', 'bug')),
    data TEXT NOT NULL DEFAULT '{}'
);
