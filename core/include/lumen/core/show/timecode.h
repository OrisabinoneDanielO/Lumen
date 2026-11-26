#pragma once

#include <QString>
#include <QtGlobal>

#include <cstddef>
#include <optional>

namespace lumen::core::show {

// MIDI timecode (MTC), for timecode cues that follow another device (a
// lighting desk, a playback rig, a DAW) instead of Lumen's own audio.
//
// MTC sends the time as eight quarter-frame messages (F1 0nnndddd), one
// every quarter frame, so a whole time arrives every two frames; a full-frame
// SysEx (F0 7F dd 01 01 hh mm ss ff F7) says where the sender jumped to. The
// frame rate (24, 25, 29.97 drop-frame or 30) travels with the hours.

// Frame-rate codes as MTC sends them.
enum class MtcRate { Fps24 = 0, Fps25 = 1, Fps2997Drop = 2, Fps30 = 3 };

double framesPerSecond(MtcRate rate);

// hh:mm:ss:ff at a rate, in milliseconds of real time (drop-frame counts are
// turned into real time, so 01:00:00;00 at 29.97 is 3600 s).
qint64 timecodeToMs(int hours, int minutes, int seconds, int frames, MtcRate rate);
// "01:00:00:00" (or with ';' before the frames) at a rate; nothing if it is
// not a timecode.
std::optional<qint64> parseTimecode(const QString &text, MtcRate rate);
// Milliseconds as hh:mm:ss:ff at a rate (';' before the frames for drop-frame).
QString formatTimecode(qint64 ms, MtcRate rate);

class MtcDecoder
{
public:
    // Feeds one MIDI message received at time nowMs (any clock, in ms).
    // Returns true if it gave a new position: a complete run of eight
    // quarter frames, or a full-frame message.
    bool feed(const unsigned char *data, std::size_t size, qint64 nowMs);

    // Where the timecode is at nowMs: the last position, moved on by the time
    // since while the timecode runs. Nothing before the first position.
    std::optional<qint64> position(qint64 nowMs) const;
    // Quarter frames are arriving (within the last stopAfterMs).
    bool running(qint64 nowMs) const;
    MtcRate rate() const { return m_rate; }
    // Each new position since the last call jumped instead of following on
    // (a locate, a full-frame message, or a gap of more than a few frames).
    bool takeJump();

    static constexpr qint64 stopAfterMs = 250;

private:
    void quarterFrame(unsigned char value, qint64 nowMs);

    int m_pieces[8] = {};
    int m_have = 0;      // bit n set: piece n received in this run
    int m_expected = 0;  // the piece that should come next going forward
    MtcRate m_rate = MtcRate::Fps25;
    std::optional<qint64> m_position;
    qint64 m_positionAt = 0;  // nowMs of m_position
    qint64 m_lastQuarter = -1; // nowMs of the last quarter frame
    bool m_running = false;
    bool m_jumped = false;
};

} // namespace lumen::core::show
