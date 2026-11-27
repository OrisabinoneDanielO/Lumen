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
| World English Bible (`engwebp`) | English | Public domain |
| King James Version (`eng-kjv2006`) | English | Public domain |
| English Berean Standard Bible (`engbsb`) | English | Public domain |
| American Standard Version (1901) (`eng-asv`) | English | Public domain |
| Yoruba Bible (`yor`) | Yorùbá | Copyright © 2009, 2017 Biblica, Inc. CC BY-SA 4.0 |
| Hausa Contemporary Bible (`hausa`) | Hausa | Copyright © 2009, 2020 Biblica, Inc. CC BY-SA 4.0 |
| Igbo Bible (`ibo`) | Igbo | Copyright © 2020 Biblica, Inc. CC BY-SA 4.0 |
| Swahili ONMM (`swhonmm`) | Swahili | Copyright © 2018, 2024 Biblica, Inc. CC BY-SA 4.0 |
| French Louis Segond 1910 Bible (`fraLSG`) | French | Public domain |
| Reina Valera 1909 (`spaRV1909`) | Spanish | Public domain |
| Bible in Basic English (`engBBE`) | English | Public domain |
| Darby Translation (`engDBY`) | English | Public domain |
| Douay-Rheims 1899 (`engDRA`) | English | Public domain |
| Geneva Bible 1599 (`enggnv`) | English | Public domain |
| Young's Literal Translation (`engylt`) | English | Public domain |
| Noah Webster Bible (`engwebster`) | English | Public domain |
| Revised Version (1895) (`eng-rv`) | English | Public domain |
| Majority Standard Bible (`engmsb`) | English | Public domain |
| Literal Standard Version (`englsv`) | English | Copyright © 2020 Covenant Press. CC BY-SA 4.0 |
| Unlocked Literal Bible (`engULB`) | English | Copyright © 2017 Door43 World Missions Community. CC BY-SA 4.0 |
| Free Bible Version (`engfbv`) | English | Copyright © 2018 Dr. Jonathan Gallagher. CC BY-SA 4.0 |
| Akuapem Twi Bible (`twi`) | Twi | Copyright © 1996, 2020 Biblica, Inc. CC BY-SA 4.0 |
| Asante Twi Bible (`twiasante`) | Twi | Copyright © 1996, 2020 Biblica, Inc. CC BY-SA 4.0 |
| Ewe Bible (`ewe`) | Ewe | Copyright © 1988, 2006, 2020 Biblica, Inc. CC BY-SA 4.0 |
| Kiswahili Contemporary Version (`swhonen`) | Swahili | Copyright © 1984, 1989, 2009, 2015 Biblica, Inc. CC BY-SA 4.0 |
| Lingala Bible (`lin`) | Lingala | Copyright © 2002, 2020 Biblica, Inc. CC BY-SA 4.0 |
| Luganda Bible (`lug`) | Luganda | Copyright © 1984, 1986, 1993, 2014 Biblica, Inc. CC BY-SA 4.0 |
| Chichewa Bible (`nya`) | Chichewa | Copyright © 2002, 2016 Biblica, Inc. CC BY-SA 4.0 |
| Shona Bible (`sna`) | Shona | Copyright © 2005, 2018 Biblica, Inc. CC BY-SA 4.0 |
| Kikuyu Bible (`kik`) | Kikuyu | Copyright © 2013 Biblica, Inc. CC BY-SA 4.0 |
| Dholuo Bible (`luo`) | Dholuo | Copyright © 1980, 2002, 2003, 2020 Biblica, Inc. CC BY-SA 4.0 |
| Ndebele Bible (`nde`) | Ndebele | Copyright © 2003, 2006, 2022 Biblica, Inc. CC BY-SA 4.0 |
| Bíblia Portuguesa Mundial (`porbrbsl`) | Portuguese | Public domain |
| Lutherbibel 1912 (`deu1912`) | German | Public domain |
| Bible J.N. Darby (`frajnd`) | French | Public domain |
| Chinese Union Version (simplified) (`cmn-cu89s`) | Chinese | Public domain |
| Chinese Union Version (traditional) (`cmn-cu89t`) | Chinese | Public domain |
| Korean Bible 1910 (`kor`) | Korean | Public domain |
| Russian Synodal Bible (`russyn`) | Russian | Public domain |
| Hindi Indian Revised Version (`hin2017`) | Hindi | Copyright © 2017, 2018, 2019 Bridge Connectivity Solutions. CC BY-SA 4.0 |
| Riveduta Bibbia 1927 (`ita1927`) | Italian | Public domain |
| De Heilige Schrift 1917 (`nld`) | Dutch | Public domain |
| Vietnamese Bible 1923 (`vie1934`) | Vietnamese | Public domain |
| Malayalam Indian Revised Version (`mal`) | Malayalam | Copyright © 2017, 2019 Bridge Connectivity Solutions. CC BY-SA 4.0 |

Licences were checked against each package's `copr.htm`: the first ten on 2026-11-23, the other 34 on 2026-11-27 (all public domain or CC BY-SA 4.0). Check
again before each release. The CC BY-SA texts are shown unmodified, with their
copyright line, as the licence requires.

The first ten are the translations in the design doc; 34 more were added on 2026-11-27 (more English versions, African languages and other major languages). Right-to-left Bibles (Arabic, Hebrew, Urdu) wait until right-to-left slides are tested. eBible.org lists about 1,300
redistributable translations in over 1,000 languages; the full set is planned
as a separate offline Bible pack, built with the same tool (batch 12). Users can
also add any USFM or USFX Bible from a file.

To add a bundled translation: put its unmodified package in a folder here, add
an entry to `manifest.json` (with its SHA-256), and rebuild.
