# Lumen Presenter — Software Design Document

Nov 23, 2026 · @j

## Overview and goals

Lumen Presenter is a desktop app for running lyrics, scripture, media and slides live during worship services and events, built so a first-time volunteer can run a Sunday service and a production team can run a multi-screen conference. It targets the same space as EasyWorship (ease of use) and ProPresenter (power and flexibility), and aims to win on both at once.

**Goals**

- A volunteer with 15 minutes of training can build and run a full service.
- Nothing the audience sees ever freezes, flashes the desktop or shows the operator UI.
- One machine drives several independent outputs: main screen, stage display, livestream overlay.
- Free for everyone, works fully offline, and keeps every file on the local machine: no price, accounts or cloud services.
- Runs on Windows, macOS and Linux from one codebase.

**Non-goals for v1**

- Full video switching or multi-camera production (we feed switchers, we don't replace them).
- Audio mixing beyond simple media playback volume and output routing.
- A web-based editor or any cloud service; the desktop app and its local files are the source of truth.

## Users and use cases

The primary user is the volunteer operator in the booth on a Sunday; every design choice is tested against them first.

| Persona | Context | What they need most |
| --- | --- | --- |
| Volunteer operator | Runs slides weekly, little training, often a teenager or retiree | Big obvious controls, follow-along with the band, no way to break the screen |
| Worship / media leader | Builds the service plan during the week | Fast song import, reusable themes, prepare on a laptop and hand off to the booth PC by USB or local network |
| Production director | Large church or conference, several screens and a livestream | Multiple outputs, layers, NDI, MIDI/timecode cues, stage display |
| Pastor / speaker | Preaches from the stage | Sermon notes and scripture on a confidence monitor, countdown timer |
| Small church admin | One laptop, one projector, tight budget | Works on modest hardware, free, offline |

**Core use cases**

1. Build a service plan (playlist) with songs, scripture, announcements, video and sermon slides.
2. Run the service live: click or keyboard through slides, jump to any part of a song (chorus, bridge) on the fly.
3. Show a different view on the stage display: current and next slide, chord charts, clock, timers and messages.
4. Put lower-third lyrics and scripture over the livestream via NDI or a key/fill output.
5. Display a quick ad-hoc message ("Parent of child 42, please come to the nursery") without stopping the slides.
6. Run a pre-service loop of announcements with a countdown to start time.

## Feature set

Twelve modules make up the product; eight ship in the MVP because a normal Sunday service needs them.

| Module | Key capabilities | Phase |
| --- | --- | --- |
| Song library | Import from CCLI SongSelect, plain text, OpenLyrics, EasyWorship and ProPresenter files; auto-split lyrics into slides by section (Verse 1, Chorus, Bridge); arrangements (V1 C V2 C B C); chords and transposition; copyright and CCLI number footer; usage reporting | MVP |
| Scripture | Multiple installed translations; type a reference ("jn 3 16-18") to jump; side-by-side versions; auto-fit long passages across slides; verse-by-verse paging | MVP |
| Media | Images, video (H.264, HEVC, ProRes), audio; looping backgrounds; motion backgrounds behind lyrics; thumbnails; trim in and out points; playback controls and scrubbing | MVP |
| Presentations | Import PowerPoint (.pptx) and PDF as image slides; Keynote via PDF; native slide editor for announcements and sermon points | MVP |
| Service plan (playlist) | Drag items from any library into an ordered plan; headers and notes; save, reuse and duplicate plans; import and export plan files | MVP |
| Themes and editor | Reusable themes per item type (song, scripture, lower third); text boxes, shapes, images, live clock and timer elements; safe-area guides; auto-shrink text to fit | MVP (basic), v2 (full) |
| Live control | Layered output (background, slide, props, messages, masks); transitions per layer; Clear buttons per layer and Clear All; Logo and Black screens; keyboard map (number keys jump to section, arrows step); undo-free live actions | MVP |
| Stage display | Separate layouts showing current and next slide, chords, notes, clock, video countdown, timers and messages to stage | v2 |
| Overlays and props | Lower thirds, bugs and logos that stay up across slides; alpha-channel output for streaming | v2 |
| Timers and messages | Countdown to time or duration, elapsed clocks; message templates with fields ("Parent of {number}") shown as a ticker or banner | MVP (basic), v2 (full) |
| Remote and automation | Phone/tablet remote; MIDI, OSC and HTTP API triggers; timecode-driven cues | v3 |
| Collaboration | Share libraries and plans between machines over the local network or by export file; no cloud | v3 |

## UI layout and workflows

The main window is one screen with five fixed regions, so the operator never hunts through menus during a service; the UI mockups show it in full.

| Region | Position | Contents |
| --- | --- | --- |
| Toolbar | Top | Show / Edit mode switch, output on/off, Clear buttons (Background, Slide, Props, Messages, All), Logo, Black, timers |
| Service plan | Left column | Ordered items of today's service with icons for type; drag to reorder; current item highlighted |
| Slide grid | Centre | Thumbnails of the selected item's slides, colour-coded by section label (Verse blue, Chorus red, Bridge purple); click a slide to send it live |
| Preview and Live | Right column | Preview of the next slide above, live output below, with the layer stack and transport controls |
| Library | Bottom drawer | Tabs for Songs, Scripture, Media, Presentations, Themes; search box; drag into plan or click to show |

**Two modes**

- **Show mode** (default on Sunday): editing is locked down; single click sends a slide live. Accidental edits can't reach the screen.
- **Edit mode**: the slide grid becomes an editor with a canvas, text styling and theme panel. Changes go live only after Save.

**Key workflows**

1. *Build a service:* New plan → search library → drag songs, scripture and media in → set arrangement per song → save. Target: under 10 minutes for a typical 8-item service.
2. *Run live:* Click the first slide or press Space; arrow keys step; letter keys jump to sections (C = Chorus, B = Bridge, V then a number = verse); Esc-style Clear keys per layer.
3. *Quick scripture:* Press the Scripture shortcut, type "rom 8 28", press Enter → verse goes live in the scripture theme in about 2 seconds.
4. *Ad-hoc message:* Messages button → pick template → fill the field → Show; it overlays without touching the slide layer.

**Keyboard map (defaults, all remappable)**

| Key | Action |
| --- | --- |
| Space / Right arrow | Next slide |
| Left arrow | Previous slide |
| Page Down / Page Up | Next / previous plan item |
| F1 to F5 | Clear background, slide, props, messages, all |
| F6 / F7 | Logo / Black |
| C, B, P, E | Jump to Chorus, Bridge, Pre-chorus, Ending |
| Ctrl + B | Open scripture quick search |

## System architecture

The app runs as two processes: the operator UI with core services, and a separate GPU render process that owns every audience-facing output.

&#91;embedded content: System architecture · UI, core services, renderer, control API\]

The UI sends commands to the show controller, which pushes scene state to the renderer over IPC; video frames go straight from the media service to the compositor as shared GPU textures. If the UI process dies, the renderer holds the last frame and the UI reconnects on restart.

**Recommended tech stack**

| Layer | Choice | Why |
| --- | --- | --- |
| Language | C++20 | Real-time rendering control, mature media and broadcast SDKs (NDI, DeckLink) are C/C++ |
| UI toolkit | Qt 6 with QML | Decided: one cross-platform UI codebase for Windows, macOS and Linux, GPU-accelerated |
| Rendering | Qt RHI (Direct3D 11/12 on Windows, Metal on macOS, Vulkan or OpenGL on Linux) | Abstracts the GPU APIs without a game engine |
| Media | FFmpeg with hardware decode (DXVA2/D3D11VA, VideoToolbox, VA-API and NVDEC on Linux) | Broad codec support, low CPU use |
| Storage | SQLite plus media files on disk | Fast, embedded, no server to install |
| IPC | Shared memory for frames, local socket for commands | Low latency, survives a UI crash |
| Scripture and song text | ICU and HarfBuzz for text shaping | Correct complex scripts and diacritics |

Electron was considered for faster UI development but rejected: frame timing on multiple outputs is unreliable and memory use is high on church laptops.

**Platform support**

Windows, macOS and Linux are all first-class targets from the MVP, built and tested from the same codebase on every release.

| Area | Windows | macOS | Linux |
| --- | --- | --- | --- |
| GPU API | Direct3D 11/12 | Metal | Vulkan, OpenGL fallback |
| Hardware video decode | D3D11VA, DXVA2 | VideoToolbox | VA-API (Intel, AMD), NVDEC (NVIDIA) |
| Packaging | MSIX / MSI installer | Signed, notarised .dmg | AppImage and Flatpak for any distro; .deb and .rpm repositories for Debian/Ubuntu and Fedora/RHEL |
| CPU | x86-64, ARM64 | Apple silicon, Intel | x86-64, ARM64 |
| Display server | n/a | n/a | X11 and Wayland |
| NDI and DeckLink | Supported | Supported | Supported via vendor Linux SDKs |

On Linux, "all distros" in practice means any distro with glibc 2.35 or newer through AppImage or Flatpak; musl-based distros such as Alpine are out of scope. Wayland doesn't let apps place windows on a chosen screen or keep them on top, so output windows request fullscreen on a specific output (supported by GNOME and KDE), fall back to XWayland, and the output setup screen warns when a desktop can't guarantee placement.

## Data model

Everything the audience sees is a Slide belonging to a Presentation, and a service plan is just an ordered list of references to Presentations; this keeps one renderer for songs, scripture and announcements alike.

| Entity | Key fields | Relationships |
| --- | --- | --- |
| Library | id, name, folder path, last backup | Has many Presentations and MediaAssets |
| Presentation | id, kind (song, scripture, slides, media), title, theme\_id, metadata (author, CCLI no., copyright, key, tempo) | Has many Groups, Arrangements, Slides |
| Group | id, label (Verse 1, Chorus), colour, hotkey | Belongs to Presentation; has ordered Slides |
| Arrangement | id, name, ordered group\_ids | Belongs to Presentation; one is default |
| Slide | id, elements\[\], notes, chords, transition override | Belongs to Group |
| Element | type (text, image, video, shape, timer, clock), frame (x, y, w, h), style, z-order | Belongs to Slide or Theme |
| Theme | id, name, applies\_to, background, element templates, safe area | Used by many Presentations |
| MediaAsset | id, file path, hash, duration, thumbnail, loop and trim settings | Referenced by Elements and Plan items |
| Plan | id, name, date, ordered PlanItems | Has many PlanItems |
| PlanItem | id, presentation\_id, arrangement\_id, header text, notes | Belongs to Plan |
| Output | id, screen/NDI target, resolution, layout (audience, stage, key/fill) | Has a Layer configuration |
| StageLayout | id, widgets (current, next, clock, notes, timers, message) | Assigned to an Output |
| Message / Timer | template text, fields, duration or target time | Shown on Message layer and stage displays |

**Storage:** a local SQLite database for the library index and plans, with media kept as files on disk referenced by path and content hash. Presentations are also exportable as a single open JSON-based file (`.lumen`) bundling slides and media, for sharing between churches and for backups to a local folder or USB drive. Nothing is stored remotely.

## Output and rendering pipeline

Output is rendered by a separate GPU process that composites a fixed stack of layers at a locked frame rate, so a crash or stall in the operator UI can never freeze the audience screen.

**Layer stack (bottom to top)**

1. Background: colour, image, looping video or live camera input
2. Media: full-screen video playback
3. Slide: the current slide's text and elements
4. Props: lower thirds, logos and bugs that persist across slides
5. Messages: banners and tickers
6. Masks: edge-blend, keystone and crop for odd screens or LED walls

Each layer has its own transition (cut, fade, dissolve, push) and its own Clear control. Logo and Black are overrides above everything.

**Outputs**

- Each physical display, NDI stream or key/fill pair is an Output with its own resolution, frame rate and layer filter (for example, the stream output shows only Slide and Props with a transparent background).
- Stage displays are Outputs that render a StageLayout instead of the audience view.
- Outputs open as borderless, always-on-top fullscreen windows that the OS cursor and notifications cannot enter.

**Performance targets**

| Measure | Target |
| --- | --- |
| Slide change, click to screen | Under 50 ms |
| Frame rate | Locked 60 fps at 1080p, 30 fps at 4K on a mid-range GPU |
| Video start latency | Under 200 ms from click, preloaded when in Preview |
| Outputs per machine | 4 (1080p) on a mid-range GPU |
| Dropped frames during a 2-hour service | Zero on supported hardware |

**Text rendering:** GPU text with signed-distance-field fonts, auto-shrink to fit, outlines, drop shadows and per-line backgrounds, with right-to-left and complex-script support (Arabic, Hebrew, Indic, Yoruba diacritics).

## Integrations

Integrations are plugins behind a common interface, so the core app ships small and each one can be updated or switched off separately.

| Integration | Purpose | Notes |
| --- | --- | --- |
| CCLI SongSelect | Search and import lyrics and chords | Decided: pursue. Apply for the CCLI partner agreement now; needs the church's own SongSelect login; internet used only during import, songs then stored locally |
| CCLI usage reporting | Log songs shown for licence reporting | Tallied locally and exported as a report file the church submits itself |
| Bible translations | Install free translations as local modules | Every translation whose licence allows free redistribution is bundled in the installer; more can be added from a file (see Free Bible translations below) |
| NDI | Send outputs over the network to switchers and streaming PCs; receive NDI as a background | NDI SDK licence terms apply |
| Blackmagic DeckLink / AJA | SDI key and fill outputs | Vendor SDKs |
| MIDI, OSC, HTTP/WebSocket API | Trigger slides, clear layers and fire cues from consoles, lighting desks and Stream Deck | Local network only |
| Timecode (LTC/MTC) | Sync slide cues to a backing track | v3 |
| Remote app | Phone or tablet control and stage view over local Wi-Fi | Paired by QR code, LAN only, no internet relay |
| Import from competitors | EasyWorship, ProPresenter, OpenLP, OpenLyrics song files | Key to winning switchers |

**Free Bible translations**

Yes: enough free translations exist to ship a strong scripture library at no cost, including Yoruba, Hausa and Igbo. They come under two kinds of licence: public domain (use freely) and CC BY-SA 4.0 (free to use and redistribute, with the copyright notice shown and the text left unmodified if it keeps the Biblica name).

| Translation | Language | Licence |
| --- | --- | --- |
| [Berean Standard Bible (BSB)](https://berean.bible/terms.htm) | English | Public domain since 30 April 2023 |
| World English Bible (WEB) | English | Public domain |
| King James Version (KJV) | English | Public domain (Crown rights still apply in the UK) |
| American Standard Version (ASV, 1901) | English | Public domain |
| [Open Yoruba Contemporary Bible](https://ebible.org/bible/details.php?id=yor) | Yoruba | CC BY-SA 4.0, Biblica |
| [Open Hausa Contemporary Bible](https://ebible.org/bible/details.php?id=hausa) | Hausa | CC BY-SA 4.0, Biblica |
| [Open Igbo Contemporary Bible](https://ebible.org/ibo/copyright.htm) | Igbo | CC BY-SA 4.0, Biblica |
| [Open Kiswahili Contemporary Scriptures](https://ebible.org/bible/details.php?id=swhonmm) | Swahili | CC BY-SA 4.0, Biblica |
| Louis Segond (1910) | French | Public domain by age |
| Reina-Valera (1909) | Spanish | Public domain by age |

eBible.org distributes these as USFM, USFX and SWORD files, so one USFM/USFX importer covers them and hundreds more; the installer bundles every translation whose licence allows free redistribution (public domain, CC BY, CC BY-SA, and no-derivatives licences kept unmodified), with no download needed at service time. If the full set makes the installer too large, it ships as a separate offline Bible pack. Each slide or the About screen shows the translation's copyright line. Popular copyrighted versions (NIV, ESV, NKJV) and the Bible Society of Nigeria's Yoruba Bible would need the publisher's permission, so they stay out unless a free licence is granted. Rows without links are from general knowledge; check each licence again before release.

## Non-functional requirements

Reliability during the live service outranks every other requirement, including new features.

| Area | Requirement |
| --- | --- |
| Reliability | Output keeps the last frame if the UI process crashes; UI restarts and reconnects in under 5 seconds; autosave every 30 seconds; library backups daily |
| Offline | Every feature works with no internet; no licence check, account or sign-in |
| Hardware floor | Windows 10/11, macOS 13+, and Linux with glibc 2.35+ (Ubuntu 22.04+, Debian 12+, Fedora 39+ and others) on X11 or Wayland; 8 GB RAM; integrated GPU supports one 1080p output; dedicated GPU recommended for 2+ outputs or 4K |
| Startup | App ready in under 10 seconds; output windows restore to the right screens automatically |
| Accessibility | Full keyboard operation; high-contrast operator theme; screen-reader labels in the operator UI; minimum audience font size warnings |
| Localization | UI translatable (English, French, Spanish, Portuguese, Yoruba, Igbo, Hausa, Swahili in first wave); Unicode and RTL lyrics |
| Security and privacy | Remote app LAN-only and PIN-paired; all data stays on local disk; no telemetry |
| Licensing | Free for everyone with every feature; no licence keys, tiers or watermarks |
| Updates | Never auto-update during a scheduled service window; rollback to previous version in one click; update checks are optional and offline installers are available |

## MVP and roadmap

The MVP is what a single-projector church needs on a normal Sunday, built batch by batch with no paid developers (see Build plan); production and connected features follow.

&#91;embedded content: Roadmap · 3 phases over 18 months, 2 release gates\]

Each release ships only after it passes its gate in real services, not just in testing; the durations are planning estimates to revisit once the team is staffed.

## Build plan

The app is built in 20 small batches, and each one must leave it building, launching and passing every earlier test on Windows, macOS and Linux before the next starts. Batches 1 to 12 make the MVP; the rest follow the roadmap.

| # | Batch | Done when |
| --- | --- | --- |
| 1 | Repo, CMake, Qt 6, CI on all three OSes, empty main window, test framework | CI builds and runs tests on Windows, macOS and Linux |
| 2 | Data layer: SQLite schema, migrations, entities from the Data model, repository API | Unit tests create, read, update and delete every entity |
| 3 | Render process: separate process, fullscreen window on a chosen screen, draws a colour and text | Killing the UI process leaves the last frame on screen |
| 4 | IPC and show controller: layer model, cut and fade, Clear per layer, Logo, Black | Scripted commands produce the expected frames in a test harness |
| 5 | Operator UI shell: five regions, Show and Edit modes, slide grid, Preview and Live, keyboard map | Clicking or pressing Space sends a slide live in under 50 ms |
| 6 | Song library: text and OpenLyrics import, auto-split, groups, arrangements, search | 20 sample songs import and display correctly |
| 7 | Scripture: USFM/USFX importer, reference parser, bundled free Bibles, quick search | "jn 3 16-18" shows the right verses in every bundled Bible |
| 8 | Themes: basic theme editor, auto-shrink text, safe area, Yoruba/Igbo/Hausa diacritics | Long verses fit; diacritics render correctly |
| 9 | Media: FFmpeg with hardware decode, backgrounds, motion loops, preload in Preview | A 2-hour loop plays with zero dropped frames on the reference PC |
| 10 | Presentations: PPTX and PDF import as slides | Sample decks import with the right slide count and order |
| 11 | Service plans, .lumen export and import, autosave, scheduled local backups | A plan survives a crash and restores from backup |
| 12 | Timers, basic messages, installers for all three OSes, offline check | MVP gate: 10 pilot churches, 4 Sundays, zero output failures |
| 13 | Stage display layouts | Current, next, clock and message show on a second screen |
| 14 | Props, lower thirds, messages with fields | Props persist across slides and clear independently |
| 15 | NDI and SDI outputs with alpha | Stream output shows only Slide and Props on transparency |
| 16 | Full slide editor | Every element type can be added, styled and saved |
| 17 | EasyWorship, ProPresenter and OpenLP import; SongSelect once the agreement is granted | Real libraries from each import without errors |
| 18 | Library and plan sharing over the local network | Two PCs exchange a plan with no internet connection |
| 19 | Phone and tablet remote; MIDI, OSC and HTTP API | Remote and a MIDI controller drive slides on the LAN |
| 20 | Timecode cues | Slides follow a backing track to the frame |

**Rules for every batch**

- One batch per branch, merged only when CI is green on all three OSes and the batch's own tests pass.
- New code comes with tests; earlier tests are never deleted to make a batch pass.
- Changing an interface an earlier batch built means updating its tests in the same batch.
- Unfinished features stay behind a setting that is off by default, so the main branch always runs a service.

## Risks and decisions

The biggest risks are the time the SongSelect agreement takes and protecting local-only data; Bible licensing is solved by free translations.

| Risk | Impact | Mitigation |
| --- | --- | --- |
| SongSelect partner agreement takes months | Weaker song import at launch | Ship text, OpenLyrics and competitor-file import first; scripture is covered by free translations |
| Video playback stalls on low-end PCs | Frozen screen on Sunday, lost trust | Hardware decode only, preload in Preview, a hardware check tool at install |
| Import from competitor formats is incomplete | Switching friction | Prioritise song text and arrangements over exact styling; publish a migration guide |
| Scope creep toward a full production switcher | Slips the MVP | Hold the non-goals; integrate with switchers over NDI and SDI |
| Unpaid build time runs short | Releases slip or half-built features break the app | Build in small batches that each leave the app working (see Build plan) |
| Local-only data is lost with a failed disk | A church loses its whole library | Scheduled backups to a second drive or USB stick, with one-click restore |

**Decisions**

- **Funding:** no paid developers; the app is built in-house, batch by batch.
- **Bibles:** bundle every freely redistributable translation in the installer.
- **SongSelect:** pursue; apply for the CCLI partner agreement now and build the importer once it is granted.
