# Batch 9 video soak: runs so far

The "Done when" for batch 9 is a 2-hour loop with zero dropped frames on the reference PC (see `docs/decisions.md`). These are the runs made so far, all on the reference laptop (i5-1155G7, Iris Xe, Fedora 45, Wayland, VA-API), playing `tests/data/media/loop-1080p-vp9.webm` as a looping background under changing lyrics, in a window.

| Date (UTC) | Planned | Played | Dropped | Pauses | Outcome |
| --- | --- | --- | --- | --- | --- |
| 2026-11-23 11:xx | 2 h | 5 min | 0 | 0 | Stopped by Qt Test's 5-minute limit per test (fixed: the soak raises the limit) |
| 2026-11-23 13:17 | 2 h | 349 s | 0 | 1 (223 ms) | Stopped at the first pause (fixed: pauses are now logged and the run goes on) |
| 2026-11-23 13:31 | 2 h | about 60 min of play | 0 | 7 | Not valid: the laptop went to sleep twice (15:04 to 15:26 and until 16:42 local) |

What the third run showed:

- **No dropped frames** in about an hour of playback (108,026 frames, 360 loops), all decoded in hardware.
- **Every pause lined up with the render process itself being held up**, not with the decoder: two while the window was opening, one when the window stopped being drawn for 3.5 minutes (covered or on another workspace), and the rest around the two sleeps.
- A renderer from the first run was still running in the background (that run's test process had been stopped by the time limit, and a renderer outlives its client by design). Tests now pass `--watch-pid` so their renderer exits with them.

Changes for the reference run:

- Counting starts 5 seconds after the video starts (start-up time is tested by `tst_video_start`).
- `LUMEN_SOAK_SCREEN=<n>` plays fullscreen, as a real output does.
- A run during which the computer sleeps fails as invalid.
- Run it with sleep blocked: `systemd-inhibit --what=sleep:idle --why="Lumen soak" ...`.

The valid 2-hour reference result will be added here.
