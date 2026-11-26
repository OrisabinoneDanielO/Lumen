# Lumen Presenter

Free, offline, cross-platform worship presentation software. Licensed under GPL-3.0 (see `LICENSE`).

The operator UI bundles the [Inter](https://github.com/rsms/inter) typeface (v4.1, SIL Open Font License 1.1, see `app/ui/fonts/OFL.txt`).

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
docs/       design doc and diagrams
design/     UI mockups
```
