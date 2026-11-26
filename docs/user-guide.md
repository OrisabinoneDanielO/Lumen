# Lumen Presenter user guide

How to use each part of Lumen. For building and contributing, see the [README](../README.md) and [CONTRIBUTING](../CONTRIBUTING.md).

## Installing

- **Windows:** run the `.msi`. Lumen Presenter is added to the Start menu and the desktop. The installer is not signed yet: if SmartScreen stops it, choose **More info → Run anyway**.
- **macOS:** open the `.dmg` and drag Lumen Presenter to Applications. It is not notarised yet, so the first time macOS blocks it: on macOS 15 or newer, open **System Settings → Privacy & Security** and click **Open Anyway**; on older versions, right-click the app and choose **Open**.
- **Linux:** the AppImage runs on any distribution (make it executable and double-click it). There are also a Flatpak, a `.deb` for Debian 13 and Ubuntu 25.04 or newer, and an `.rpm` for Fedora.

Step-by-step instructions for each system are in the [README](../README.md#install). Everything Lumen needs is included, the Bibles too, and it never needs the internet. To check an installation, run `lumen --check-install` (on Windows, `lumen.exe --check-install > check.txt`); every line should start with `ok`.

## Starting

Start Lumen from the Start menu, Applications or your desktop's app list (or `lumen` in a terminal). The first time, the library is empty: import songs (from files or another program), add media and decks, and make a plan for the service. The bundled Bibles are there from the start.

## Screens

The first time Lumen starts with more than one screen connected (and whenever a new screen is plugged in), each screen shows its number, name and role in large type, and the **Screens** panel opens:

- The screen with the operator window is marked **this screen**.
- For each other screen choose **Main output** (the audience), **Stage** (the stage display: current and next slide, clock, timers and messages) or **Not used**. Each role goes to one screen; giving it to another screen takes it from the first.
- The outputs move as you choose. Giving a screen the Stage role turns the stage display on; taking it away turns it off.
- **Done** closes the panel. Lumen remembers the roles for this set of screens and puts the outputs back on them at the next start without asking.

Open it again any time with **Plan ▾ → Screens…**.

## Updates

Lumen never looks for updates or installs anything unless you ask. **Plan ▾ → Check for updates…** asks GitHub, where Lumen is published, whether a newer version exists; if there is one, **Download** opens its page in your browser, and you install it as you installed Lumen (your library is kept). Turn on **Check when Lumen starts** to check once a little after each start; the Plan menu then says when a newer version is available. A check sends nothing about you or the computer.

## The audience output

The audience output runs in its own process, `lumen-render`, so a crash in the operator UI never freezes the screen. It starts with the app, fullscreen on the screen chosen in **Screens** (at first, the first screen that is not the operator's); with one screen it opens as a window. `--render-option=--screen=N` overrides the choice. Click **Main** in the toolbar to turn it off or on (remembered), or start with `--no-output`.

## Running a service

In Show mode, click a slide to send it live, or use the keyboard: Space or → next slide, ← previous, Page Down/Up next/previous plan item, F1–F5 clear background/slide/props/messages/all, F6 Logo, F7 Black, C/B/P/E jump to Chorus/Bridge/Pre-chorus/Ending, V then a number for a verse, 1–9 for a section. Edit mode (the slide editor; set `experimental/editor` to false to hide it) edits a draft of the selected item and nothing is stored or shown until **Save**. Slides presentations take text boxes, shapes, pictures, videos, clocks and countdowns, dragged into place on the canvas and styled on the right, with a background picture or motion loop and a transition per slide; **New slides** makes a new presentation in the plan. Songs are edited as lyrics: a blank line starts a slide, a line like [Verse 2] starts a group.

## Songs

**Import songs…** or **Import folder…** in the library reads plain-text lyrics (`.txt`, with or without `Verse 1`/`Chorus` headers) and OpenLyrics (`.xml`). Sections are split into slides of 2 lines (setting `songs/linesPerSlide`; a `---` line forces a break). Search matches the start of words in titles, authors, CCLI numbers and lyrics, ignoring case and accents. Click a song to show it, then **Add to plan**.

## Scripture

Press **Ctrl+B** (Cmd+B on macOS), type a reference such as `jn 3 16-18`, `rom 8:28-30`, `ps 23` or a translation's own book name (`Johanu 3:16`), pick a translation and press Enter. Ten free Bibles are bundled (English WEB, KJV, BSB and ASV; Yoruba, Hausa, Igbo and Swahili; French LSG; Spanish RV1909); see `bibles/README.md` for their licences. The build imports them into `bibles.db`. Other USFM or USFX Bibles can be added from a folder.

## Themes

**Library > Themes** edits the look of songs, scripture and lower thirds (font, sizes, colour, alignment, outline, shadow, background, margins and the footer) with a live preview. Text shrinks to fit the theme's safe area and long passages split across slides. The output uses the bundled Noto Sans and Noto Serif fonts (SIL OFL 1.1, `core/fonts/OFL.txt`), which cover Yoruba, Igbo and Hausa letters and tone marks.

## Songs from other software

**Import songs…** (or **Import folder…**) also reads an OpenLP library (`songs.sqlite`), an EasyWorship 6/7 library (`Songs.db` with `SongWords.db` beside it, in EasyWorship's `Databases/Data` folder) and ProPresenter 5 and 6 documents (`.pro5`, `.pro6`), with their sections, order, authors, copyright and CCLI numbers. ProPresenter 7 documents cannot be read: export the songs as text from ProPresenter and import those. SongSelect import waits for the CCLI partner agreement.

## Media

The **Media** tab in the library imports videos (`.mp4`, `.mov`, `.webm`, `.mkv` and more) and pictures. Each gets a thumbnail; videos of a minute or less are marked as loops. Click a card to show it in the slide grid and send it live like a slide (it fills the screen; Clear Slide stops it), or press **Use as background** to play it as a silent motion loop under songs and scripture. Videos are decoded in the render process with hardware decoding where the system offers it. The video in Preview is opened ahead of time, so it starts at once when it goes live.

## Presentations

The **Presentations** tab imports PowerPoint (`.pptx`, `.ppt`, `.odp`) and PDF files as picture slides, one per slide, in show order (hidden slides are left out). PowerPoint files are converted by PowerPoint (Windows) or LibreOffice when one is installed, so they look exactly as designed; without either, Lumen draws `.pptx` slides itself (backgrounds, pictures, shapes and text, but not charts, tables or effects) and the import report says so. For Keynote, export to PDF first. Optional: install LibreOffice (`sudo dnf install libreoffice-impress`) for exact PowerPoint slides.

## Plans and backups

The **Plan** menu above the service plan makes, opens, renames and duplicates plans, adds headers, and exports or imports a plan as a `.lumen` file (the plan, its slides and its media in one file, for the booth PC or another church). Right-click an item to move it, add notes or remove it. Everything is saved as you go; the open plan and item are remembered every 30 seconds, so Lumen reopens where you were after a crash. A backup of the library is made daily (the newest 14 are kept, in `backups` beside the library), and **Restore a backup** puts one back (Lumen restarts; the current library is kept beside it).

## Timers and messages

**Timers** in the toolbar starts, pauses and resets countdowns (for a set time or to a time of day) and count-up clocks, and shows one in the banner at the top of the output. Countdown slides, such as a pre-service loop, show the time live. The output counts on its own clock, so a timer keeps time even if the operator window stops. **Message** puts a line of text in the banner over the slides and keeps recent ones a click away; pick a template ("Parent of {number}, please come to the nursery"), fill in its fields and show it, as a banner or a scrolling ticker. Clear Msg (F4) takes it down.

## Props

**Props** in the toolbar keeps lower thirds (a name and a title) and logos for a corner. Click one to put it up or take it down; a lower third and a logo can be up together, and they stay up while the slides change until you take them down or press Clear Props (F3).

## Stage display

Click **Stage** in the toolbar to turn on a confidence monitor for the musicians and speaker. It shows the slide on air (NOW) and the next one, the group, key, notes and arrangement, the time of day, a timer and a message to the stage only ("Repeat the last verse"). It runs in its own render process on a third screen (a window when there is none), in one of three layouts: Default, Lyrics (now and next, large) or Clock (time and timer, large).

## Stream output

Click **Stream** in the toolbar for an output with only the slide text and props, on transparency, for streaming software and video switchers. It shows as key and fill (the picture over black and its transparency as grey, in two windows, or fullscreen on two spare screens) for switchers that take key and fill over HDMI. NDI is experimental (setting `experimental/ndi`): with the free NDI Runtime installed (from ndi.video) the stream is sent as an NDI source with its transparency. Nothing from NDI ships with Lumen. SDI cards (DeckLink) are not supported yet.

## Sharing between computers

No internet needed: **Plan ▾ → Share with other computers…**. On the computer to share, press **Share this computer**; it shows a six-digit code and its address. On the other, pick it from the list (computers on the same network find each other) or type its address, enter the code, and copy its plans or songs, or send it the open plan. Media come along. Sharing is off until turned on; a firewall must allow Lumen (TCP on the port shown, and UDP 45459 for finding each other).

## Remote control

**Plan ▾ → Remote control and MIDI…**. The phone and tablet remote is a web page Lumen serves on the local network: turn it on, open the address it shows on a phone on the same Wi-Fi, enter the code, and use Next, Previous, Clear, Logo, Black, the plan and the slides (no app, no internet). OSC (UDP, port 8000) takes `/lumen/next`, `/lumen/previous`, `/lumen/slide 3`, `/lumen/clear/slide`, `/lumen/black` and more, for show-control software and tablet apps. MIDI controllers (pads, keyboards, foot switches) drive the slides too: by default C4 is Next, B3 Previous, G4 Black, A4 Logo, and a program change picks a plan item; **Learn** gives any action the next key, pad or pedal pressed. The same commands are an HTTP API: `GET /api/state`, `POST /api/next` and so on, with the code in an `X-Lumen-Code` header. All of these are off until turned on.

## Timecode cues

**Cues** above the slides gives the selected item a backing track (an audio file) and cues: times in the track, each with a slide. **Record** plays the track and turns each slide you send live into a cue at that moment; nudge any cue a frame earlier or later; **Play on output** has the output play the track and change slides on the frame. The cues and the track travel in `.lumen` files.

**Following MIDI timecode.** When a lighting desk, playback rig or DAW sends MIDI timecode (MTC), choose **Follow: MIDI timecode** instead of a backing track. **Device** is part of the MIDI input's name (empty: any input); **Cue 0:00 at** is the timecode where the song starts (often `01:00:00:00`). **Record**, then start the timecode on the other device and send each slide live at its moment: each cue is timed by the timecode. **Play on output**: the output follows the timecode on its own, changes slides on the frame, stops when the timecode stops, and goes back when the other device jumps back. 24, 25, 29.97 drop-frame and 30 fps are understood.

## Offline

Lumen makes no internet connections (sharing, when turned on, talks only to computers on the local network). `tests/offline_check.sh` checks this on Linux by tracing the app and its output, and by running them with no network at all.
