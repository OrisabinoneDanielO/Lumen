# How Lumen works

A guide for people changing Lumen's code. For the product design see [design-doc.md](design-doc.md); for why things are the way they are, batch by batch, see [decisions.md](decisions.md).

## Processes

Lumen is several programs that talk over local sockets:

```
 lumen  (the operator window)                         lumen-render  (one per output)
 ┌──────────────────────────────────────────┐         ┌───────────────────────────────┐
 │ QML UI (app/ui)                           │         │ audience output  (default)     │
 │   │ properties and calls                  │  JSON   │ stage display    (--stage)     │
 │ controllers (app/operator)                │  lines  │ stream output    (--stream)    │
 │   │                                       │ ──────► │                                │
 │ ShowController (the scene) ─ OutputBridge ├─────────┤ RenderServer → OutputWindow     │
 │ SQLite library (core/data)                │ local   │   SceneModel → QML (Output.qml, │
 │ songs, scripture, media, plans, backups   │ socket  │   Stage.qml, StreamScene.qml)  │
 └──────────────────────────────────────────┘         └───────────────────────────────┘
```

- **`lumen`** is the operator window and everything that is not drawing an output: the library, plans, importers, controllers, and the network features (sharing, the phone remote, OSC, MIDI).
- **`lumen-render`** draws an output with Qt Quick on the GPU (Qt RHI). It is started detached, so if the operator window crashes or is killed the output keeps showing its last frame, and the operator window reconnects when it starts again (it asks the renderer what is on air and carries on from there). This is the design's main reliability rule: *the operator UI must never be able to freeze an output.*
- The same `lumen-render` program runs as the **stage display** (`--stage`: now, next, clock, timer and a message for the musicians) and the **stream output** (`--stream`: only the Slide and Props layers, drawn off screen on transparency for key and fill or NDI). Each is its own process, so trouble in one cannot reach the others.

Video and the backing tracks for timecode cues are decoded in the render process, beside the GPU, never in the operator window.

## Source layout

| Folder | Target | What is in it |
| --- | --- | --- |
| `core/src/data`, `include/lumen/core/data` | `Lumen::Data` | SQLite database, schema migrations (`migrations/*.sql`), entities (`entities.h`), `Repository<T>` (create, read, update, delete, list children) |
| `core/.../show` | `Lumen::Show` | `Scene` (the six layers and what each shows), `ShowController` (the one place that changes the scene), `OutputBridge` (sends scenes to a renderer), text fitting (`textstyle.h`) |
| `core/.../render` | `Lumen::RenderLink` | `RenderLink` (starts or finds a renderer, sends requests), `protocol.h` (the protocol, documented) |
| `core/.../songs` | `Lumen::Songs` | Song parsing (text, OpenLyrics, OpenLP, EasyWorship, ProPresenter), auto-split, the song library and its search index |
| `core/.../scripture` | `Lumen::Scripture` | Bibles (USFM/USFX import, the Bible store) and the reference parser (`jn 3 16-18`) |
| `core` (top) | `Lumen::Core` | `Result`/`Status` error handling, version, text folding for search |
| `app/operator` | `Lumen::Operator` | The controllers the QML binds to (below), and the features built on the core: media, presentations, plans, backups, `.lumen` files, timers, props, stage, stream, editor, sharing, remote control, cues |
| `app/ui` | `lumen_ui` | The operator window's QML, after `design/mockups/*.html` |
| `app/main.cpp` | `lumen` | Wires everything together: opens the library, creates the controllers, starts the outputs, loads `Main.qml` |
| `render` | `lumen_render_lib`, `lumen-render` | The renderer: `RenderServer` (requests), `OutputWindow` (a window or an off-screen stream), `SceneModel` (the scene as QML sees it, transitions, timers, cue runners), `VideoMonitor`, key and fill, NDI |
| `render/slides` | `Lumen.Slides` (QML) | How content is drawn (`SlideContent.qml`, `ComposedSlide.qml`), shared by the outputs and the operator window's Preview and Live frames so they look the same |
| `tools` | `lumen-bible-build` | Imports the bundled Bibles into `bibles.db` at build time |
| `tests` | `tst_*` | Qt Test programs; `operatorharness.h` loads the real operator window for UI tests |

## The scene and its layers

Everything on an output is a `Scene` (`core/include/lumen/core/show/scene.h`): six layers, bottom to top, plus the Logo and Black overrides.

| Layer | Typical content |
| --- | --- |
| Background | a theme's colour or picture, or a motion loop |
| Media | a video or picture sent as a slide |
| Slide | the slide: text, a composed editor slide, a countdown, or cued slides |
| Props | a lower third and/or a logo that stays up across slides |
| Messages | a banner, a ticker or a timer |
| Masks | an opaque fill over everything (for odd screens) |

Each layer holds one `Content`, with a kind: `colour`, `text`, `image`, `video`, `timer`, `prop`, `composed` or `cued`, and a transition (cut or fade). Only `ShowController` changes the scene; every change gets a new sequence number and is sent to each output by its `OutputBridge`.

Content is described, not computed, wherever time is involved: a timer is sent as "ends at 10:00:00 wall-clock time", cued slides as "the track and the times", so the renderer counts on its own clock and keeps time even if the operator window stops.

## How a slide gets to the screen

1. The operator clicks a slide or presses Space. `Main.qml` calls `OperatorController::goLive(index)` (keys go through `KeyMap` and `handleKey`).
2. `goLive` builds the slide's `Content` (`slideContent`: themed text, composed elements, a countdown, a picture or a video) and asks `ShowController` to change the Slide layer (and the Background for a themed item).
3. `ShowController` updates the scene and emits `sceneChanged`.
4. `OutputBridge` sends `{"cmd":"scene","scene":{...}}` to each renderer over its local socket, without waiting.
5. `RenderServer` hands the scene to `OutputWindow`, whose `SceneModel` starts each changed layer's transition.
6. QML (`Output.qml` → `LayerStack.qml` → `SlideContent.qml`) draws the layers; the next swapped frame shows the slide. The renderer reports the sequence number it presented, which the latency test uses (under 50 ms from click to frame).

Phone, MIDI, OSC and HTTP commands go through `LiveControl`, which calls the same controller functions as the keyboard, so every way in behaves the same (and nothing goes live in Edit mode).

## Data

One SQLite file holds the library and the plans (`library.db` in the app's data folder; schema in `core/src/data/migrations`). Media files stay where the user keeps them, referenced by path and a content fingerprint. The entities follow the design doc's data model:

```
Library ── Presentation (song, scripture, slides, media) ── Group ── Slide ── Element
               │  └─ Arrangement (group order)                        (text, image, video,
               │                                                       shape, timer, clock)
Plan ── PlanItem (a presentation or a header, with notes)
MediaAsset   Theme   Timer   Message (templates)   Prop   StageLayout   Output
```

- Every change is written at once inside a transaction (`Database::inTransaction`); there is no unsaved state. `synchronous = FULL` keeps the last change through a power cut.
- Schema changes are new numbered migration files; a database written by a newer Lumen is refused rather than touched.
- Errors are returned (`Result<T>`, `Status`), never thrown, so a storage problem cannot take down the app during a service.
- `.lumen` files ([lumen-file.md](lumen-file.md)) are zip archives with a JSON description and the media, used for exporting plans and for sharing between computers.
- Backups are complete copies of the database (`VACUUM INTO`), made daily; restoring happens at the next start.

## The render protocol

JSON objects, one per line, over a `QLocalSocket`; every request carries the protocol version `"v"` and gets a reply with `"ok"`. The commands and their fields are documented at the top of `core/include/lumen/core/render/protocol.h`: `scene`, `getScene`, `status`, `preload`, `stage`, `clock` (test mode), `grab` (test mode), `texts` (test mode), `quit`. A renderer refuses requests from a newer protocol version.

`status` reports what tests and diagnostics need: frames presented, the presented sequence, video statistics (dropped frames, stalls), timing of the render loop, the timeline of cued slides, the screen and the kind of output.

## The operator window (QML and C++)

`Main.qml` lays out the regions from `design/mockups/Main.html`: toolbar, service plan, slide grid (or the editor in Edit mode), Preview and Live, library. QML never touches the database: it binds to properties of C++ controllers passed in as initial properties:

| Controller | Purpose |
| --- | --- |
| `OperatorController` | the plan, the selected item and its slides, going live, clear/logo/black, plans and backups, messages, the frames' content |
| `ScriptureController`, `ThemeController` | scripture search, the theme editor |
| `TimerController`, `PropsController`, `MessageTemplates` | timers, lower thirds and logos, message templates |
| `StageController`, `StreamController` | the stage display's state, the stream output's settings |
| `EditorController` | the slide editor's draft and Save |
| `Sharing`, `RemoteServer`, `OscServer`, `MidiInput`, `LiveControl` | the network features and remote control |
| `CueController` | timecode cues |

Lists are `RowModel`s (a list of maps with named roles); `updateRows` changes rows in place for values that tick.

## Threads

Almost everything runs on the main thread of each process. Exceptions: presentation import draws slides on a worker thread (`PresentationLibrary::render`, which touches no database), Qt Quick renders on its render thread, video decoding is Qt Multimedia's own, and MIDI messages arrive on RtMidi's thread and are passed to the main thread with a queued call.

## Settings

`QSettings` holds preferences (`output/enabled`, `stage/layout`, `remote/enabled`, `midi/mapping`, ...); `app/main.cpp` lists every key near the top. Unfinished or untested features stay behind settings that are off by default (`experimental/ndi`).

## Tests

Each batch's "Done when" condition has a test, and earlier tests are never weakened. They run offscreen with Qt Quick's software renderer, so they need no display:

- **Unit tests** for the core (`tst_database`, `tst_repositories`, `tst_song_import`, `tst_scripture`, ...).
- **UI tests** load the real operator window through `tests/operatorharness.h` and click and type in it (`tst_operator_window`, `tst_plans`, `tst_editor`, ...).
- **Renderer tests** run an `OutputWindow` in the test process on a manual clock (`tst_render_layers`, `tst_timers`, `tst_timecode`), or start the real `lumen-render` (`tst_send_live_latency`, `tst_stage`, `tst_stream`, `tst_rehearsal`).
- **Process tests**: the UI killed while the output survives (`tst_render_survives_ui_kill`), a plan editor crashing mid-edit (`tst_plans`), a second process acting as another PC (`tst_sharing`), the offline check (`offline_check.sh`), and the same sharing test with no network (`tst_sharing_offline`).

Tests that start renderers pass `--watch-pid`, so a crashed test never leaves a renderer running.
