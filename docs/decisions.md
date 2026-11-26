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

The audience output starts with the app (setting `output/enabled`, default on; `--no-output` to skip it; `--output` starts it even when the setting is off). The **Main** light in the toolbar turns it off and on.

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

## Batch 16: the slide editor

### Drafts and Save

The editor works on a draft of one presentation and writes nothing until Save; Cancel throws the draft away. Save replaces the presentation's groups, slides and elements in one transaction and keeps its id, so plans that use it carry on (a plan item's chosen arrangement falls back to the default if the arrangement was rebuilt). Edit mode still never reaches the screen; it hides the Preview and Live panel, as in the editor mockup, and keeps the service plan to pick what to edit.

### Composed slides

Slides presentations made or changed in the editor hold positioned elements (text, shape, image, video, clock, timer) on a 1920 x 1080 grid. Every element the editor makes carries a style, and a slide with any styled element is sent to the output as composed content: the element list, drawn by `ComposedSlide.qml` in the shared Lumen.Slides module, so the output, the Preview and Live frames and the editor canvas look the same. Clocks and countdowns are counted from the renderer's own clock (the countdown rule is also in `timertext.js`, matching `core::show::timerText`). Older slides (songs, scripture, imported decks, the sample service's slides) have no styled elements and keep their look.

### Songs

Songs are edited as lyrics text, as in the mockup: a blank line starts a slide within a group and a line like [Verse 2] starts a group. The editor turns that into the song importer's terms ("---" between slides) and saves through the same parsing and auto-split rules as importing, then rebuilds the default arrangement and the search entry. Song slides keep their theme's look; the Insert tools are for slides presentations.

### On by default

After batch 16 the user chose to show Edit mode by default; `experimental/editor` set to false hides it again.

## Batch 17: songs from EasyWorship, ProPresenter and OpenLP

### Test libraries, not real ones

No real libraries were available, so (as the user chose) the tests use libraries written by `tests/data/libraries/make_libraries.py` in each program's layout: OpenLP's `songs.sqlite` (songs, authors, authors_songs; lyrics as OpenLP's verse XML with a verse order), EasyWorship 6/7's `Songs.db` and `SongWords.db` (lyrics as RTF), and ProPresenter 6 and 5 XML documents. They show the importers read those layouts; real libraries may hold things the test files do not (other RTF, older or newer schema versions). The first real libraries should be tried as soon as there are any, and their quirks added to the test files.

### What is read

- OpenLP: title, authors (display names), copyright, CCLI number, verses by type (v, c, b, p, i, e, o) with OpenLP's [---] slide breaks, and the verse order.
- EasyWorship: title, authors (split at ; , / and "and"), copyright with the administrator, the reference number as the CCLI number, and the lyrics: RTF turned into text (Windows-1252 and \u characters decoded), section names on their own line, a blank line between slides (the editor's rule, now shared in `slideBreaksFromBlankLines`).
- ProPresenter 5 and 6: the CCLI fields, each group as a section (common names such as "Verse 1" or "Chorus" become Lumen's labels), each slide's text (plain text where stored, else the RTF), and the first arrangement as the play order.

### ProPresenter 7 and SongSelect

ProPresenter 7 stores documents as protocol buffers with no published schema; a reader built from guesses could only be tested against files built from the same guesses. The importer says so and points to ProPresenter's text export. SongSelect waits for the CCLI partner agreement, as the design doc says.

## Batch 18: sharing plans and songs on the local network

### Direct, with a code, off by default

Computers talk directly over TCP on the local network; there is no server and nothing leaves the building (CLAUDE.md allows LAN features). Sharing is off until the operator turns it on, and then the computer shows a six-digit code that another computer must give to see or send anything, so other people on a church's Wi-Fi cannot browse the library. After five wrong codes a computer has to wait a minute (six digits cannot be guessed like that). The offline check still passes, because a computer that is not sharing announces nothing.

### Finding each other

Sharing computers announce themselves every two seconds on a local multicast group (239.255.76.77, UDP 45459); the Share popover listens while it is open, so a computer can find others without sharing itself. Where multicast is blocked (some guest Wi-Fi, a loopback-only test), the address can be typed in; the popover shows it on the shared computer.

### What travels

A `.lumen` file (batch 11): a plan with its songs, slides and media, or chosen songs without a plan (the file then has no "plan" and importing adds none). Importing reuses songs and media the library already has, so copying next week's plan does not duplicate the songs. Transfers wait in a local event loop, so the window keeps drawing; the outputs are separate processes and are never affected.

### Two PCs, no internet

`tst_sharing` runs a second process with its own library as the other PC: a plan is copied from it, songs are copied, and a plan is sent to it, over TCP. CTest also runs it as `tst_sharing_offline`, inside a network namespace with nothing but loopback (`tests/no_network.sh`), where the exchange works and only multicast discovery is skipped. On the real network the discovery test found the other computer. Two physical PCs have not been tried yet.

## Batch 19: phone remote, OSC, MIDI and the HTTP API

### One set of commands

The phone remote, the HTTP API, OSC and MIDI all go through `LiveControl`, which calls the same operator actions as the keyboard and the window. So they behave the same, and, like the keyboard, never send anything live in Edit mode.

### The phone remote is a web page

Lumen serves one self-contained page (`app/operator/remote/remote.html`, built in) from a small HTTP server of its own on the local network: no app to install, nothing from the internet. The page polls `/api/state` and posts commands. Off by default; when on, it shows a six-digit code (as with sharing; five wrong codes and a minute's wait), which the phone gives once (`?code=` in the link works too, for a QR code later). A small built-in server was chosen over Qt HttpServer to add no module for CI and the installers to carry.

### OSC

UDP (port 8000 by default), messages and bundles, int/float/string/bool arguments. Numbers are 1-based (`/lumen/slide 1` is the first slide), as people count; the HTTP API is 0-based like JSON arrays. A button that sends 1 on press and 0 on release acts once. No code: OSC tools have no way to send one, so it is off by default and should be turned on only on a trusted network.

### MIDI

RtMidi 6.0.0 (MIT, `thirdparty/rtmidi`, unchanged) gives MIDI input on Linux (ALSA), Windows (Windows Multimedia) and macOS (CoreMIDI); without ALSA headers Linux builds without MIDI and the popover says so. Every MIDI input is listened to, and new devices are picked up within three seconds. Default notes (C4 next, B3 previous, D4/A3 next/previous item, E4 clear slide, F4 clear all, G4 black, A4 logo; program change N selects item N); a note acts on press, a control change on its rise past 64 (foot pedals). Learn gives an action the next message and takes that message from any other action. The test drives Lumen from a virtual MIDI device (an ALSA virtual port); no hardware controller has been tried.

## Batch 20: timecode cues

### The render process runs the cues

The operator side sends the whole timeline at once (the track, and every cue's slide as content) as cued content on the Slide layer. The render process plays the track and, every frame, shows the cue whose time has come, with a cut: a cue appears on the first frame at or after its time, without waiting on the operator window, which could be busy. Slides only go forward within a run.

### Keeping to the audio

Qt's media player reports the track's position in steps of one audio buffer (about 85 ms here), each the end of the buffer just handed to the sound card. So the timeline's clock is the render clock, started when playing starts, and eased towards the middle of each step; a difference over 250 ms (a stall or a seek) is jumped. Measured here: every cue on its frame (0 to 15 ms after its time at 60 frames per second), the clock within 34 ms of the audio's reported position. The audio device's own delay is not known to Qt, so a sound system with a long delay will hear slides early by that delay; a per-output offset can be added if churches need it. Without a track, or without an audio device, the render clock alone keeps time.

### Cues are kept with the item

In the presentation's metadata ("timeline": the backing track's media asset and the cues as times and slides in play order). A changed arrangement can point cues at other slides; the Cues popover shows each cue's slide so this is visible. `.lumen` files carry the track and the cues.

### External timecode

Following an external clock (MIDI timecode or LTC from a playback system) is not in this batch; the cue runner takes its clock from one place, so MTC through the MIDI input (batch 19) is the natural next step.

## No sample content in the app

The app once had `--design-preview`, which loaded the mockup's sample service into a temporary library to compare the window with `design/mockups`. It was removed before the first release so the app ships no sample or mock content: a first start shows an empty library. The sample service is now test data only (`tests/support/sampleservice.cpp`), used by the operator tests.

## The screen setup

Which screen shows the main output and which the stage display used to follow a fixed rule (the first screen that is not the operator's, then the next), changeable only with a command-line option. Volunteers could not tell which screen was which, so a **Screens** panel now opens the first time a set of screens is connected: each screen shows its number in large type, and the operator gives each a role. The roles are remembered per set of screens (by connector, monitor model and size), meeting the design doc's "output windows restore to the right screens automatically".

- The labels are drawn by the renderer on the screens it is on (protocol v10 "identify") and by the operator window elsewhere, so a label never hides behind a fullscreen output.
- A running output moves with the protocol's "screen" command instead of restarting, so the picture is kept. Showing windows do not move between screens reliably on every platform, so the renderer hides, moves and shows the window again: a blink, only during setup.
- The operator window stays on the primary screen; choosing the operator's screen is left to the system's display settings.

## Update checks

The design doc makes update checks optional and forbids updating during a service. Lumen's check is off by default (so a fresh install makes no network connection: `offline_check` still passes) and runs only when the operator asks or turns on "Check when Lumen starts". It reads GitHub's "latest release" for the repository (drafts and pre-releases are left out) and compares versions. It never downloads or installs: "Download" opens the release page in the browser, and the operator installs the new version when it suits them, never in the middle of a service. The request carries only a `LumenPresenter/<version>` user agent. Rolling back is installing the previous release from the Releases page.

