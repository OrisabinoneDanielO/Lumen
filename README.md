# Lumen Presenter

Free, offline, cross-platform worship presentation software. Licensed under GPL-3.0 (see `LICENSE`).

The operator UI bundles the [Inter](https://github.com/rsms/inter) typeface (v4.1, SIL Open Font License 1.1, see `app/ui/fonts/OFL.txt`); the audience output bundles Noto Sans and Noto Serif (SIL OFL 1.1, see `core/fonts/OFL.txt`).

- Design doc: `docs/design-doc.md` (export it from claude.ai, see `docs/README.md`)
- Diagrams: `docs/diagrams.md`
- UI mockups: open `design/mockups/index.html` in a browser
- Working with Claude Code: see `CLAUDE.md`

## Building

Needs CMake 3.24+, Ninja, a C++20 compiler and Qt 6.5+ (Core, Gui, Qml, Quick, Multimedia, Pdf, Xml, Test).

```sh
cmake --preset debug          # or: release
cmake --build --preset debug
ctest --preset debug          # runs every test, offscreen
./build/debug/app/lumen       # macOS: build/debug/app/lumen.app
```

On Fedora, the tools can live in a toolbox container instead of the host:

```sh
toolbox create lumen
toolbox run -c lumen sudo dnf install -y cmake ninja-build gcc-c++ \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtmultimedia-devel qt6-qtpdf-devel qt6-qtbase-private-devel mesa-libGL-devel
toolbox run -c lumen cmake --preset debug   # and so on
```

The audience output runs in its own process, `lumen-render`, so a crash in the operator UI never freezes the screen. It starts with the app: with one screen it opens as a window; with more it goes fullscreen on the first screen that is not the operator's (`--render-option=--screen=N` to choose). Click **Main** in the toolbar to turn it off or on (remembered), or start with `--no-output`. `--design-preview` leaves it off unless `--output` is given.

In Show mode, click a slide to send it live, or use the keyboard: Space or → next slide, ← previous, Page Down/Up next/previous plan item, F1–F5 clear background/slide/props/messages/all, F6 Logo, F7 Black, C/B/P/E jump to Chorus/Bridge/Pre-chorus/Ending, V then a number for a verse, 1–9 for a section. `lumen --design-preview` loads the mockup's sample service into a temporary library. Edit mode (the slide editor) is behind the setting `experimental/editor`: it edits a draft of the selected item and nothing is stored or shown until **Save**. Slides presentations take text boxes, shapes, pictures, videos, clocks and countdowns, dragged into place on the canvas and styled on the right, with a background picture or motion loop and a transition per slide; **New slides** makes a new presentation in the plan. Songs are edited as lyrics: a blank line starts a slide, a line like [Verse 2] starts a group.

Songs: **Import songs…** or **Import folder…** in the library reads plain-text lyrics (`.txt`, with or without `Verse 1`/`Chorus` headers) and OpenLyrics (`.xml`). Sections are split into slides of 2 lines (setting `songs/linesPerSlide`; a `---` line forces a break). Search matches the start of words in titles, authors, CCLI numbers and lyrics, ignoring case and accents. Click a song to show it, then **Add to plan**.

Scripture: press **Ctrl+B** (Cmd+B on macOS), type a reference such as `jn 3 16-18`, `rom 8:28-30`, `ps 23` or a translation's own book name (`Johanu 3:16`), pick a translation and press Enter. Ten free Bibles are bundled (English WEB, KJV, BSB and ASV; Yoruba, Hausa, Igbo and Swahili; French LSG; Spanish RV1909); see `bibles/README.md` for their licences. The build imports them into `bibles.db`. Other USFM or USFX Bibles can be added from a folder.

Themes: **Library > Themes** edits the look of songs, scripture and lower thirds (font, sizes, colour, alignment, outline, shadow, background, margins and the footer) with a live preview. Text shrinks to fit the theme's safe area and long passages split across slides. The output uses the bundled Noto Sans and Noto Serif fonts (SIL OFL 1.1, `core/fonts/OFL.txt`), which cover Yoruba, Igbo and Hausa letters and tone marks.

Media: the **Media** tab in the library imports videos (`.mp4`, `.mov`, `.webm`, `.mkv` and more) and pictures. Each gets a thumbnail; videos of a minute or less are marked as loops. Click a card to show it in the slide grid and send it live like a slide (it fills the screen; Clear Slide stops it), or press **Use as background** to play it as a silent motion loop under songs and scripture. Videos are decoded in the render process with hardware decoding where the system offers it. The video in Preview is opened ahead of time, so it starts at once when it goes live.

Presentations: the **Presentations** tab imports PowerPoint (`.pptx`, `.ppt`, `.odp`) and PDF files as picture slides, one per slide, in show order (hidden slides are left out). PowerPoint files are converted by PowerPoint (Windows) or LibreOffice when one is installed, so they look exactly as designed; without either, Lumen draws `.pptx` slides itself (backgrounds, pictures, shapes and text, but not charts, tables or effects) and the import report says so. For Keynote, export to PDF first. Optional: install LibreOffice (`sudo dnf install libreoffice-impress`) for exact PowerPoint slides.

Plans and backups: the **Plan** menu above the service plan makes, opens, renames and duplicates plans, adds headers, and exports or imports a plan as a `.lumen` file (the plan, its slides and its media in one file, for the booth PC or another church). Right-click an item to move it, add notes or remove it. Everything is saved as you go; the open plan and item are remembered every 30 seconds, so Lumen reopens where you were after a crash. A backup of the library is made daily (the newest 14 are kept, in `backups` beside the library), and **Restore a backup** puts one back (Lumen restarts; the current library is kept beside it).

Timers and messages: **Timers** in the toolbar starts, pauses and resets countdowns (for a set time or to a time of day) and count-up clocks, and shows one in the banner at the top of the output. Countdown slides, such as a pre-service loop, show the time live. The output counts on its own clock, so a timer keeps time even if the operator window stops. **Message** puts a line of text in the banner over the slides and keeps recent ones a click away; pick a template ("Parent of {number}, please come to the nursery"), fill in its fields and show it, as a banner or a scrolling ticker. Clear Msg (F4) takes it down.

Props: **Props** in the toolbar keeps lower thirds (a name and a title) and logos for a corner. Click one to put it up or take it down; a lower third and a logo can be up together, and they stay up while the slides change until you take them down or press Clear Props (F3).

Stage display: click **Stage** in the toolbar to turn on a confidence monitor for the musicians and speaker. It shows the slide on air (NOW) and the next one, the group, key, notes and arrangement, the time of day, a timer and a message to the stage only ("Repeat the last verse"). It runs in its own render process on a third screen (a window when there is none), in one of three layouts: Default, Lyrics (now and next, large) or Clock (time and timer, large).

Stream output: click **Stream** in the toolbar for an output with only the slide text and props, on transparency, for streaming software and video switchers. It shows as key and fill (the picture over black and its transparency as grey, in two windows, or fullscreen on two spare screens) for switchers that take key and fill over HDMI. NDI is experimental (setting `experimental/ndi`): with the free NDI Runtime installed (from ndi.video) the stream is sent as an NDI source with its transparency. Nothing from NDI ships with Lumen. SDI cards (DeckLink) are not supported yet.

Offline: Lumen makes no network connections. `tests/offline_check.sh` checks this on Linux by tracing the app and its output, and by running them with no network at all.

Linux AppImage: see `packaging/README.md`.

CI (`.github/workflows/ci.yml`) builds and tests on Windows, macOS and Linux with Qt 6.8 LTS.

## Layout

```
app/        operator UI (QML + C++ glue)
core/       data layer, show controller, importers
render/     render process (RHI compositor, output windows)
plugins/    integrations (SongSelect, NDI, DeckLink, MIDI/OSC)
bibles/     bundled translation files and their licences
tests/      unit and integration tests
packaging/  installers for Windows, macOS, Linux
tools/      build-time tools (lumen-bible-build)
docs/       design doc and diagrams
design/     UI mockups
```
