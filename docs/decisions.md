# Decisions

Choices made while building, with the reasons. The fixed decisions are in `CLAUDE.md` and the design doc.

## Batch 9: media

### Videos are decoded in the render process

The UI process only sends file paths (and preload requests) over the render link. `lumen-render` opens, decodes and draws each video itself, with Qt Multimedia's FFmpeg backend.

- **Why:** the decoded frames stay on the GPU next to the compositor (VA-API, D3D11 or VideoToolbox textures), with no copy of 1080p frames between processes. A UI crash or freeze cannot stop a video, which is the same reason the renderer is separate in the first place.
- **Cost:** the UI does not see the frames. Preview and Live show a thumbnail of the video (made on import), not the moving picture.
- The UI process decodes one frame per file when importing, only to make the thumbnail and read the length.

### Preload in Preview

When the slide in Preview (the one Space sends next) is a video, the UI asks the renderer to `preload` it. The renderer opens it in a hidden player and decodes the first frame, and keeps the last two. Measured with `tst_video_start` (renderer receiving the scene to first frame, offscreen): 1080p VP9 took about 145 ms cold and 25 to 32 ms preloaded. The requirement is under 200 ms from the click; the UI adds under 50 ms (batch 5).

### Reference PC

The "2-hour loop with zero dropped frames" test runs on the developer's laptop: Intel i5-1155G7 with Iris Xe graphics, 16 GB RAM, Fedora 45, Wayland, OpenGL RHI, VA-API (intel-media-driver). The result is in `docs/test-results/`.

### Codecs

On Fedora the free Intel driver decodes VP9 (and AV1/HEVC on newer chips) in hardware but H.264 is decoded in software (OpenH264), because of patents. Windows (D3D11/DXVA) and macOS (VideoToolbox) decode H.264 in hardware. The soak test uses a VP9 loop so the reference run uses the hardware path; `LUMEN_REQUIRE_HW=1` makes it fail if any frame does not.

### How dropped frames are counted

Each frame's presentation time says where it sits in the video. If the next frame shown is more than 1.5 frame lengths later, the frames in between were skipped. A jump back to the start is a loop; frames missing before the end or after the start count as dropped. A gap of more than 100 ms between shown frames counts as a stall.

### Motion backgrounds

**Use as background** plays a video looped and muted on the Background layer. While the operator has chosen a background, themed items no longer replace it with their theme's background; Clear Background (F1) hands control back to the themes. Videos sent as slides go on the Media layer with sound and play once; Clear Slide stops them too.

## Batch 10: presentations

### Slides come in as pictures

As the design doc says, imported decks become one picture per slide (1920 wide, or fitted to 1080 high for upright pages), shown on the Media layer like any picture. Text in them cannot be edited in Lumen; the native slide editor is for Lumen's own slides.

### PowerPoint: office program if installed, else Lumen's own drawing

No free library draws `.pptx` slides faithfully. Chosen with the user:

1. On Windows with PowerPoint installed, PowerPoint converts the deck to PDF (through its automation interface, without a window). Not tested yet: no test machine has PowerPoint.
2. Otherwise, if LibreOffice is installed, it converts the deck to PDF, with a profile of its own so a LibreOffice the user has open is untouched.
3. Otherwise, or if the converted PDF has the wrong number of pages, Lumen draws each `.pptx` slide itself: backgrounds, pictures, filled shapes and text with sizes, colours, bullets and alignment taken from the slide, its layout, master and theme. Charts, tables, SmartArt and effects are not drawn. The import report says the slides were drawn by Lumen.

The PDF is drawn with Qt PDF. `.ppt` and `.odp` files need an office program; Keynote files need exporting to PDF first.

### Show order and hidden slides

The order comes from the slide list in `ppt/presentation.xml`, not from the slide file names inside the zip (they keep their creation order when slides are moved). Hidden slides are left out, as in a slide show; the report says how many.

### Reading the zip

`.pptx` files are zip archives. Qt's own zip reader (`QZipReader`) is private API, so it may change between Qt versions; it is used in one file (`pptxreader.cpp`) to keep any change small. The build needs Qt's private Gui headers (`qt6-qtbase-private-devel` on Fedora; included in the official Qt installers).

### Sample decks

`tests/data/decks/make_decks.py` writes the sample decks with the Python standard library. Each slide has its own background colour and the slide files are named out of order, so the tests check the order from pixels, both through LibreOffice and through Lumen's own drawing.

## Batch 11: plans, .lumen files, autosave and backups

### Saving

Every change to a plan or the library is written to SQLite inside a transaction as it is made; there is no unsaved state to lose. The library now uses `synchronous = FULL`, so a change is on disk when its transaction ends even if the power fails straight after (church buildings lose power). The "autosave every 30 seconds" requirement covers what is not data: which plan and item are open. That is saved to `library.db.session.json` every 30 seconds and whenever a plan is opened, and reopened on the next start, so a restart after a crash comes back where the operator was.

### Backups

A backup is a complete copy of the library database (SQLite `VACUUM INTO`, which works while the library is open). One is made at start-up and then checked hourly; a new one is made once a day has passed since the last. The newest 14 are kept, in `backups` beside the library by default (setting `backups/folder`; a second drive or USB stick is safer). Media files are not copied: they stay where they are, and the `.lumen` export bundles them when they need to travel.

Restoring cannot swap the database under a running app, so it restarts: the choice is recorded beside the library, and on start-up (before the library is opened) the backup is checked and copied into place. The replaced library is kept as `library-before-restore-<time>.db`, so a restore can itself be undone. If the library fails SQLite's check at start-up it is set aside as `library-damaged.db` and Lumen starts with an empty one, so a service can still run, and the backups are there to restore.

### .lumen files

A zip archive with `lumen.json` (the plan and its presentations) and the media files; the format is in `docs/lumen-file.md`. Importing always adds a new plan and reuses presentations and media the library already has, so importing a weekly plan from the worship leader does not pile up duplicate songs. Themes are referred to by name. Large videos are read into memory while exporting (Qt's zip writer works that way); fine for typical files, to revisit if multi-gigabyte videos become common.

### Plan menu

The mockups show no plan controls, so the "+ Add" button in the Service Plan header became a **Plan** menu: New, Open, Rename, Duplicate, Add header, Export, Import, Back up now and Restore. Right-clicking an item moves it, edits its notes or removes it. Notes show under the item in the plan.

## Batch 12: timers, messages, output, AppImage, offline

### What stands in for the MVP gate

The batch's "Done when" is a field trial (10 pilot churches, 4 Sundays, zero output failures) that only people can run. Chosen with the user, the automated stand-in is `tst_rehearsal`: the real operator window drives a real `lumen-render` through a whole service again and again (countdown slide and timer, motion background, every item of the sample service slide by slide, a video, a PowerPoint deck, a message, Logo, Black, Clear All). Any change not on screen within a second, any renderer restart and any dropped video frame is a failure. Routine runs last 90 seconds (about 13 services and 530 changes); `LUMEN_REHEARSAL_SECONDS=3600` runs a full hour. The pilot itself is still the real gate.

### Timers are counted by the renderer

A timer goes to the output as wall-clock times (when a countdown ends, when a count-up started, or the value it is paused at), not as a stream of numbers. The render process works out what to show from its own clock every frame, so a timer keeps time if the operator window freezes, and the UI only sends something when the operator starts, pauses or resets it. Countdowns stop at 0:00. The three default timers are "Service start" (a countdown to 10:00), "Sermon" (30 minutes) and "Elapsed". Whether a timer is running is not saved; after a restart countdowns are back at their full time (countdowns to a time of day are unaffected).

### Messages

Basic messages (this batch) are a line of text in the top banner on the Messages layer, with the last eight kept for reuse. Templates with fields ("Parent of {number}") are batch 14.

### The output is on by default

The audience output starts with the app (setting `output/enabled`, default on; `--no-output` to skip it; the design preview leaves it off unless `--output` is given). The **Main** light in the toolbar turns it off and on.

### Linux AppImage only

Chosen with the user: batch 12 builds an AppImage (with linuxdeploy) and leaves Windows and macOS installers, Flatpak, .deb and .rpm for later. AppImages need a glibc at least as new as the build machine's, so the CI job builds on Ubuntu 22.04. The AppImage has not been built on this laptop yet: running the downloaded linuxdeploy tools needs the user's permission.

### Offline check

`tests/offline_check.sh` (a CTest test on Linux) runs the real app and output under `strace` and fails on any internet socket (`AF_INET`/`AF_INET6`); local sockets for the render link, D-Bus and the display are allowed. It then runs both again in a network namespace with no network at all.

### Tests and stray renderers

Tests start renderers with `--watch-pid=<test pid>`, so a test that crashes or times out cannot leave a renderer playing video in the background (as happened during the batch 9 soak).

## Batch 13: stage display

### Its own render process

The stage display is a second `lumen-render`, started with `--stage` (server `<name>-stage`), not a second window of the audience renderer. A problem on the stage (a crash, a hung driver on that screen) cannot touch the audience output, which is the reason the renderer is separate in the first place. It costs one more process; both are small next to the video they decode.

### Stage state, not a second scene

The operator side sends a stage state (the item and slide on air, the next slide, group, key, notes, arrangement with the current part, a timer, a message, the layout; see `protocol.h`), not the audience layers. The stage renderer draws `render/Stage.qml`, laid out as `design/mockups/Stage.html`, and counts the clock and the timer from its own clock, like the audience timers. The state is sent only when it changes.

### Screens

The operator window is on the primary screen and the audience output on the first other one, so the stage goes on the second other screen; with fewer screens it opens as a window. It is off by default (setting `stage/enabled`) and turned on from the Stage popover (click the Stage light in the toolbar).

### Layouts

Three built-in layouts: Default (the mockup), Lyrics (now and next, large, for singers) and Clock (time and timer, large, for the speaker). The data model's StageLayout (stored widgets) waits for the layout editor in the full editor (batch 16).

### Messages to the stage

A message sent from the Stage popover shows only on the stage, never on the audience output; the audience banner (Message in the toolbar) is separate.

## Batch 14: props, lower thirds, messages with fields

### One props item, two parts

The Props layer holds one piece of content, so a prop is composed of two independent parts: a lower third (a name and a title) and a bug (a logo in a corner). The Props popover puts either up or takes it down without touching the other. Nothing else in the operator window writes to the Props layer, so props stay up across slides, items, Clear Slide, messages, Logo and Black; only Clear Props (F3), Clear All, taking them down, or deleting one removes them. What is up is read back from the scene, so the popover always agrees with the output.

Props are stored in the library in a new `props` table (schema version 4) with their settings as JSON. `tst_database`'s list of tables and `tst_repositories`' create-read-update-delete checks were extended for it (no earlier check was loosened).

### Lower third look

A dark band with an orange accent bar at the bottom left, the name in bold above the title, in Noto Sans like the slides. Themes for lower thirds (the theme target exists since batch 2) come with the full editor (batch 16).

### Message templates

Templates are Message rows ("Parent of {number}, please come to the nursery"); the fields are the names in braces, worked out from the text, so adding a template needs no separate field list. A message with an empty field is not shown. Three come built in (nursery, car lights, welcome). Any message can run as a ticker, scrolling across the banner, instead of standing still.

## Batch 15: stream output with alpha, key and fill, NDI

### A render process of its own, drawn off screen

The stream output is a third `lumen-render`, started with `--stream`. It draws only the Slide and Props layers (Background, Media, Messages and Masks are left out and decode nothing) on transparency, off screen, with Qt Quick's software renderer straight into a picture with an alpha channel. The stream carries text and pictures, never video, so software drawing is cheap, repaints only what changed, and leaves each frame in memory where key and fill and NDI need it (a GPU renderer would need a read-back from the graphics card every frame). Black takes the stream to full transparency; Logo does not apply to it.

The app's bridge to the stream follows the show (`OutputBridge::follow`) but never adopts the stream's scene, so only the main output's renderer decides what is on air after a UI crash.

### Key and fill

For switchers without alpha input: the fill is the picture over black, the key its transparency as grey. They show in two windows, or fullscreen on two spare screens (after the audience output's and the stage's).

### NDI: the user's runtime, loaded at run time; experimental

Chosen with the user: no proprietary SDK ships with Lumen (it is GPL-3.0). The NDI plugin loads the NDI Runtime the user has installed and calls its exported C functions, declared in `render/ndisink.cpp`. It has not been tested with a real runtime (none was available), so it is behind the `experimental/ndi` setting, off by default, and the Stream popover says so. Without the runtime it only reports that it is missing (tested).

### SDI (DeckLink): not yet

Blackmagic's DeckLink API is a set of C++ COM interfaces whose declarations come only with the DeckLink SDK; there is nothing to compile against without it, and no card to test with. The stream output's frames are ready for a DeckLink sink when the SDK is available.
