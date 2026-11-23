# Bundled Bibles

Freely redistributable translations shipped with Lumen Presenter. Each folder
holds the USFM package exactly as downloaded from eBible.org (unmodified; the
package's own `copr.htm` is its copyright and licence notice). `manifest.json`
lists each translation's button label, licence, the copyright line Lumen shows,
the source URL and the file's SHA-256.

At build time the packages are unpacked and imported into `bibles.db` (next to
the `lumen` executable) by `lumen-bible-build`, so the app never imports at
start-up and works fully offline.

| Translation | Language | Licence |
| --- | --- | --- |
| World English Bible (`engwebp`) | English | Public domain ("World English Bible" is a trademark of eBible.org: do not use the name for altered text) |
| King James Version (`eng-kjv2006`) | English | Public domain (Crown rights apply to printing in the UK) |
| Berean Standard Bible (`engbsb`) | English | Public domain |
| American Standard Version 1901 (`eng-asv`) | English | Public domain |
| Yoruba Bible, Biblica Open Yoruba Contemporary Bible (`yor`) | Yoruba | CC BY-SA 4.0, © 2009, 2017 Biblica, Inc. |
| Hausa Contemporary Bible (`hausa`) | Hausa | CC BY-SA 4.0, © 2009, 2020 Biblica, Inc. |
| Igbo Bible (`ibo`) | Igbo | CC BY-SA 4.0, © 2020 Biblica, Inc. |
| Swahili ONMM (`swhonmm`) | Swahili | CC BY-SA 4.0, © 2018, 2024 Biblica, Inc. |
| Louis Segond 1910 (`fraLSG`) | French | Public domain |
| Reina-Valera 1909 (`spaRV1909`) | Spanish | Public domain |

Licences were checked on 2026-11-23 against each package's `copr.htm`. Check
again before each release. The CC BY-SA texts are shown unmodified, with their
copyright line, as the licence requires.

These ten are the translations in the design doc. eBible.org lists about 1,300
redistributable translations in over 1,000 languages; the full set is planned
as a separate offline Bible pack, built with the same tool (batch 12). Users can
also add any USFM or USFX Bible from a file.

To add a bundled translation: put its unmodified package in a folder here, add
an entry to `manifest.json` (with its SHA-256), and rebuild.
