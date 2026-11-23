# Lumen Presenter

Free, offline, cross-platform worship presentation software. Licensed under GPL-3.0 (see `LICENSE`).

The operator UI bundles the [Inter](https://github.com/rsms/inter) typeface (v4.1, SIL Open Font License 1.1, see `app/ui/fonts/OFL.txt`); the audience output bundles Noto Sans and Noto Serif (SIL OFL 1.1, see `core/fonts/OFL.txt`).

- Design doc: `docs/design-doc.md` (export it from claude.ai, see `docs/README.md`)
- Diagrams: `docs/diagrams.md`
- UI mockups: open `design/mockups/index.html` in a browser
- Working with Claude Code: see `CLAUDE.md`

## Building

Needs CMake 3.24+, Ninja, a C++20 compiler and Qt 6.5+ (Core, Gui, Qml, Quick, Test).

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
    qt6-qtbase-devel qt6-qtdeclarative-devel mesa-libGL-devel
toolbox run -c lumen cmake --preset debug   # and so on
```

The audience output runs in its own process, `lumen-render`, so a crash in the operator UI never freezes the screen. It is still experimental (it shows a test frame) and off by default; try it with `lumen --experimental-output`. With one screen it opens as a window; with more it goes fullscreen on the first screen that is not the operator's (`--render-option=--screen=N` to choose).

In Show mode, click a slide to send it live, or use the keyboard: Space or → next slide, ← previous, Page Down/Up next/previous plan item, F1–F5 clear background/slide/props/messages/all, F6 Logo, F7 Black, C/B/P/E jump to Chorus/Bridge/Pre-chorus/Ending, V then a number for a verse, 1–9 for a section. `lumen --design-preview` loads the mockup's sample service into a temporary library. Edit mode is hidden until editing works (setting `experimental/editor`).

Songs: **Import songs…** or **Import folder…** in the library reads plain-text lyrics (`.txt`, with or without `Verse 1`/`Chorus` headers) and OpenLyrics (`.xml`). Sections are split into slides of 2 lines (setting `songs/linesPerSlide`; a `---` line forces a break). Search matches the start of words in titles, authors, CCLI numbers and lyrics, ignoring case and accents. Click a song to show it, then **Add to plan**.

Scripture: press **Ctrl+B** (Cmd+B on macOS), type a reference such as `jn 3 16-18`, `rom 8:28-30`, `ps 23` or a translation's own book name (`Johanu 3:16`), pick a translation and press Enter. Ten free Bibles are bundled (English WEB, KJV, BSB and ASV; Yoruba, Hausa, Igbo and Swahili; French LSG; Spanish RV1909); see `bibles/README.md` for their licences. The build imports them into `bibles.db`. Other USFM or USFX Bibles can be added from a folder.

Themes: **Library > Themes** edits the look of songs, scripture and lower thirds (font, sizes, colour, alignment, outline, shadow, background, margins and the footer) with a live preview. Text shrinks to fit the theme's safe area and long passages split across slides. The output uses the bundled Noto Sans and Noto Serif fonts (SIL OFL 1.1, `core/fonts/OFL.txt`), which cover Yoruba, Igbo and Hausa letters and tone marks.

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
