.pragma library

// What a timer shows at wall-clock time nowMs: the same rules as
// core::show::timerText (scene.cpp), for composed slides drawn in QML.
// Countdowns stop at 0:00 and round up; count-up clocks round down.
function timerText(timer, nowMs) {
    if (!timer || !timer.mode)
        return ""
    let ms
    if (timer.pausedMs !== undefined)
        ms = timer.pausedMs
    else if (timer.mode === "elapsed")
        ms = nowMs - (timer.startedAtMs !== undefined ? timer.startedAtMs : nowMs)
    else
        ms = (timer.endsAtMs !== undefined ? timer.endsAtMs : nowMs) - nowMs
    ms = Math.max(0, ms)
    const countdown = timer.mode !== "elapsed"
    const seconds = countdown ? Math.floor((ms + 999) / 1000) : Math.floor(ms / 1000)
    const h = Math.floor(seconds / 3600), m = Math.floor(seconds / 60) % 60, s = seconds % 60
    const two = n => (n < 10 ? "0" : "") + n
    return h > 0 ? h + ":" + two(m) + ":" + two(s) : m + ":" + two(s)
}
