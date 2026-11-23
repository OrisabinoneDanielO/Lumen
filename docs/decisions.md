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
