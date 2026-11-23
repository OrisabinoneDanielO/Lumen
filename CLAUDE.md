# Lumen Presenter: instructions for Claude Code

Lumen Presenter is a free, offline, cross-platform desktop app for showing lyrics, scripture, media and slides in worship services (in the space of EasyWorship and ProPresenter).

## Source of truth

- `docs/design-doc.md` is the full design: goals, features, architecture, data model, rendering, integrations, requirements and the **Build plan**. Read it before any work. If code and the doc disagree, stop and ask.
- `docs/diagrams.md` has the architecture and roadmap as Mermaid diagrams.
- `design/mockups/*.html` are the UI designs (open `index.html` in a browser). Match their layout, regions, colours and labels.

## Fixed decisions (do not change without asking)

- C++20, Qt 6 with QML, one cross-platform UI for Windows, macOS and Linux.
- Rendering through Qt RHI in a **separate render process**; the operator UI must never be able to freeze an output.
- FFmpeg with hardware decode; SQLite plus media files on local disk.
- Free for everyone: no licence keys, accounts, tiers, watermarks or telemetry.
- All data stored locally. No cloud services. The only network uses allowed: SongSelect import (user-initiated), optional update checks, and LAN features (remote app, MIDI/OSC/HTTP API, library sharing).
- Bibles: bundle every freely redistributable translation (public domain, CC BY, CC BY-SA, unmodified no-derivatives); show each one's copyright line.

## How to work: one batch at a time

The Build plan in `docs/design-doc.md` lists 20 numbered batches. For every batch:

1. Say which batch you are starting and summarise its scope and "Done when" test. Propose a plan and wait for approval before writing code.
2. Work on a branch named `batch-NN-short-name`.
3. Write tests for the batch's "Done when" condition alongside the code.
4. Build and run **all** tests (not just the new ones) before saying the batch is done.
5. Never delete or weaken an earlier test to make a batch pass. If an earlier interface must change, update its tests in the same batch and say so.
6. Anything unfinished goes behind a setting that is off by default, so the main branch can always run a service.
7. Stop at the end of the batch. Do not start the next batch until the user confirms CI is green on Windows, macOS and Linux and the branch is merged.

## Suggested layout (batch 1 creates it)

```
app/        operator UI (QML + C++ glue)
core/       data layer, show controller, importers
render/     render process (RHI compositor, output windows)
plugins/    integrations (SongSelect, NDI, DeckLink, MIDI/OSC)
bibles/     bundled translation files and their licences
tests/      unit and integration tests
packaging/  installers for Windows, macOS, Linux (AppImage, Flatpak, .deb, .rpm)
docs/       design doc and diagrams
design/     UI mockups
```
