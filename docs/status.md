# Project status

Lumen was built in the 20 batches of the design doc's build plan, each with tests for its "Done when" condition. This page says what has been tested how, so nobody mistakes "has a test" for "proven in a church". Details and reasons are in [decisions.md](decisions.md).

## The batches

| # | Batch | "Done when", and how it is tested |
| --- | --- | --- |
| 1 | Repo, CMake, Qt 6, CI, empty window | CI builds and runs tests on Windows, macOS, Linux: **CI first runs when the project is pushed to GitHub**; the tests run on every local build |
| 2 | Data layer | Every entity created, read, updated and deleted (`tst_repositories`) |
| 3 | Render process | Killing the UI leaves the last frame on screen (`tst_render_survives_ui_kill`) |
| 4 | Show controller, layers | Scripted commands give the expected frames (`tst_show_script`) |
| 5 | Operator window | A click or Space sends a slide live in under 50 ms (`tst_send_live_latency`) |
| 6 | Song library | 20 sample songs import and display correctly (`tst_song_samples`) |
| 7 | Scripture | `jn 3 16-18` correct in every bundled Bible (`tst_scripture_bundled`) |
| 8 | Themes | Long verses fit; diacritics render correctly (`tst_theme_fit`) |
| 9 | Media | A 2-hour loop with no dropped frames on the reference PC: **0 dropped frames in about an hour; the full 2-hour run is still to do** (see [test-results](test-results/batch-09-video-soak.md)); 2-minute soak on every build |
| 10 | Presentations | Sample decks import with the right slide count and order (`tst_presentations`) |
| 11 | Plans, `.lumen`, autosave, backups | A plan survives a crash and restores from backup (`tst_plans`) |
| 12 | Timers, messages, AppImage, offline | Stand-in for the pilot gate: a scripted service with no output failures (`tst_rehearsal`); no network connections (`offline_check.sh`) |
| 13 | Stage display | Current, next, clock and message on a second screen (`tst_stage`, virtual screens) |
| 14 | Props, lower thirds, message templates | Props persist across slides and clear independently (`tst_props`) |
| 15 | Stream output with alpha | Only Slide and Props, on transparency (`tst_stream`) |
| 16 | Slide editor | Every element type added, styled and saved (`tst_editor`) |
| 17 | Imports from other software | Libraries from each import without errors (`tst_foreign_import`, **generated test libraries**) |
| 18 | Sharing on the local network | Two PCs exchange a plan with no internet (`tst_sharing`, **two processes on one PC**) |
| 19 | Remote, MIDI, OSC, HTTP | A remote and a MIDI controller drive slides (`tst_remote`, **a virtual MIDI device**) |
| 20 | Timecode cues | Slides follow a backing track to the frame (`tst_timecode`) |

## Since the build plan

| What | How it is tested |
| --- | --- |
| CI on Windows, macOS and Linux | Every test on all three for each change to `main`. On CI's macOS virtual machines (no GPU) the speed checks against the reference hardware (frame rate, video start and soak, rehearsal deadlines, cue timing) run as advisory; everywhere else they are strict (`tests/support/timing.h`) |
| Installers: `.msi`, `.dmg` (Apple Silicon and Intel), AppImage, Flatpak, `.deb`, `.rpm` | Each built, installed on a clean machine or container, and checked with `lumen --check-install` (window, Bibles, renderer, first output frame); the `.msi` is also uninstalled (`.github/workflows/packages.yml`) |
| No sample content in the app | The mockup's sample service is test data only (`tests/support/sampleservice.cpp`) |
| Timecode cues following MIDI timecode (MTC) | `tst_mtc` (timecode arithmetic, drop-frame, the decoder); `tst_mtc_cues` (frame by frame on a stepped clock, stopping, locating back, and a virtual MIDI device in real time); `tst_timecode` (recording against timecode, what the output gets) |
| Optional update checks (off by default; never install anything) | `tst_updates` against a local stand-in for GitHub: newer, same, older, no release, nonsense, no answer; the Plan menu and panel |
| Screens setup: which screen is the main output, which the stage | `tst_screens` on three virtual screens: roles, memory, labels, the panel, the renderer moving, and the app restoring a setup |

## Tested only on one machine, or with stand-ins

Everything has run on the developer's laptop (Fedora 45, Intel Iris Xe, Wayland). Not yet tried:

- **Windows and macOS**: built, tested and installed by CI only; not yet used on a real Windows PC or Mac.
- **Real song libraries** from EasyWorship, ProPresenter and OpenLP (tests use files written to each program's layout).
- **Real MIDI hardware** (tests use a virtual MIDI port), **a real phone** with the remote (the page was checked in a desktop browser at phone size), **two physical PCs** sharing (tests use two processes), **real extra screens** for the screens setup and the stage display (tests use virtual screens).
- **PowerPoint conversion through Microsoft PowerPoint** on Windows (LibreOffice conversion is tested).
- **NDI**: written against the NDI Runtime's exported functions but never run with one; behind the `experimental/ndi` setting.
- **The installers** (`.msi`, `.dmg`, AppImage, Flatpak, `.deb`, `.rpm`): built, installed and checked with `lumen --check-install` by CI on clean machines; not yet installed on a church's computers.
- **The full 2-hour video soak** on the reference PC.

## Not done

- ProPresenter 7 documents (an unpublished binary format; ProPresenter can export text).
- SongSelect import (waits for the CCLI partner agreement).
- SDI output through DeckLink cards (needs Blackmagic's SDK).
- Code signing for Windows and macOS (needs certificates); MSIX; apt/dnf repositories and Flathub.
- Following LTC (audio timecode). (MIDI timecode is followed, and a sound delay setting compensates for mixers and wireless.)
- Translations of the interface (texts are marked with `qsTr`; no translations yet).
- The MVP gate in the design doc: 10 pilot churches for 4 Sundays.

## Quality

The latest full check is [qa/2026-11-26.md](qa/2026-11-26.md): what was tested, what was fixed, and a ranked list of what is still to do.

## Good places to help

- **Try it in a real service** (or a rehearsal) and report what goes wrong.
- **Build and test on Windows or macOS**, and fix what CI or real use turns up.
- **Send song libraries** (or anonymised copies) from EasyWorship, ProPresenter or OpenLP that do not import well; add their quirks to `tests/data/libraries/make_libraries.py`.
- **Test with hardware**: MIDI controllers, NDI receivers, phones and tablets, multi-screen setups, LED walls.
- **Translations**: Lumen is used by churches in many languages.
- **Packaging**: code signing and notarisation, MSIX, Flathub, apt and dnf repositories.
- **ProPresenter 7 import**, if you know the format well enough to test against real files.
