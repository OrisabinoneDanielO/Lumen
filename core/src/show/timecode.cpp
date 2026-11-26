#include "lumen/core/show/timecode.h"

#include <QRegularExpression>

#include <cmath>
#include <cstdlib>

namespace lumen::core::show {

double framesPerSecond(MtcRate rate)
{
    switch (rate) {
    case MtcRate::Fps24:
        return 24;
    case MtcRate::Fps25:
        return 25;
    case MtcRate::Fps2997Drop:
        return 30000.0 / 1001.0;
    case MtcRate::Fps30:
        return 30;
    }
    return 25;
}

qint64 timecodeToMs(int hours, int minutes, int seconds, int frames, MtcRate rate)
{
    if (rate == MtcRate::Fps2997Drop) {
        // Drop-frame: frame labels 0 and 1 are skipped at the start of every
        // minute except each tenth, so the labels keep up with real time.
        const qint64 totalMinutes = qint64(hours) * 60 + minutes;
        const qint64 frameNumber = (totalMinutes * 60 + seconds) * 30 + frames - 2 * (totalMinutes - totalMinutes / 10);
        return qint64(std::llround(double(frameNumber) * 1001.0 / 30.0));
    }
    const int fps = int(framesPerSecond(rate));
    return ((qint64(hours) * 60 + minutes) * 60 + seconds) * 1000 + qint64(frames) * 1000 / fps;
}

std::optional<qint64> parseTimecode(const QString &text, MtcRate rate)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(\\d{1,2})[:.](\\d{2})[:.](\\d{2})[:;.](\\d{2})\\s*$"));
    const auto match = pattern.match(text);
    if (!match.hasMatch())
        return std::nullopt;
    const int h = match.captured(1).toInt(), m = match.captured(2).toInt(), s = match.captured(3).toInt(),
              f = match.captured(4).toInt();
    if (h > 23 || m > 59 || s > 59 || f >= int(std::ceil(framesPerSecond(rate))))
        return std::nullopt;
    return timecodeToMs(h, m, s, f, rate);
}

QString formatTimecode(qint64 ms, MtcRate rate)
{
    if (ms < 0)
        ms = 0;
    int h = 0, m = 0, s = 0, f = 0;
    if (rate == MtcRate::Fps2997Drop) {
        // Back from real time to a frame number, then to drop-frame labels.
        // (Frames to milliseconds rounds to the nearest ms; the 0.02-frame
        // allowance takes that back, so labels survive a round trip.)
        qint64 frame = qint64(std::floor(double(ms) * 30.0 / 1001.0 + 0.02));
        const qint64 perTenMinutes = 17982; // 10 * 60 * 30 - 9 * 2
        const qint64 perMinute = 1798;      // 60 * 30 - 2
        const qint64 tens = frame / perTenMinutes;
        qint64 rest = frame % perTenMinutes;
        frame += 18 * tens;
        if (rest > 1)
            frame += 2 * ((rest - 2) / perMinute);
        f = int(frame % 30);
        s = int(frame / 30 % 60);
        m = int(frame / 1800 % 60);
        h = int(frame / 108000 % 24);
        return QStringLiteral("%1:%2:%3;%4")
            .arg(h, 2, 10, QLatin1Char('0'))
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'))
            .arg(f, 2, 10, QLatin1Char('0'));
    }
    const int fps = int(framesPerSecond(rate));
    const qint64 totalSeconds = ms / 1000;
    f = int((ms % 1000) * fps / 1000);
    s = int(totalSeconds % 60);
    m = int(totalSeconds / 60 % 60);
    h = int(totalSeconds / 3600 % 24);
    return QStringLiteral("%1:%2:%3:%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(f, 2, 10, QLatin1Char('0'));
}

bool MtcDecoder::feed(const unsigned char *data, std::size_t size, qint64 nowMs)
{
    if (size >= 2 && data[0] == 0xF1) {
        const std::optional<qint64> before = m_position;
        const qint64 beforeAt = m_positionAt;
        quarterFrame(data[1], nowMs);
        return m_position != before || m_positionAt != beforeAt;
    }
    // Full frame: F0 7F <device> 01 01 hr mn sc fr F7.
    if (size >= 10 && data[0] == 0xF0 && data[1] == 0x7F && data[3] == 0x01 && data[4] == 0x01) {
        m_rate = MtcRate((data[5] >> 5) & 0x03);
        m_position = timecodeToMs(data[5] & 0x1F, data[6] & 0x3F, data[7] & 0x3F, data[8] & 0x1F, m_rate);
        m_positionAt = nowMs;
        m_running = false; // a locate: it runs again when quarter frames come
        m_have = 0;
        m_expected = 0;
        m_jumped = true;
        return true;
    }
    return false;
}

void MtcDecoder::quarterFrame(unsigned char value, qint64 nowMs)
{
    const int piece = (value >> 4) & 0x07;
    const int bits = value & 0x0F;
    // A gap, or pieces out of order (playing backwards, a lost message):
    // start collecting again from piece 0.
    if ((m_lastQuarter >= 0 && nowMs - m_lastQuarter > stopAfterMs) || piece != m_expected) {
        m_have = 0;
        if (piece != 0) {
            m_expected = 0;
            m_lastQuarter = nowMs;
            return;
        }
    }
    m_lastQuarter = nowMs;
    m_pieces[piece] = bits;
    m_have |= 1 << piece;
    m_expected = (piece + 1) % 8;
    if (piece != 7 || m_have != 0xFF)
        return;

    m_have = 0;
    m_rate = MtcRate((m_pieces[7] >> 1) & 0x03);
    const int frames = m_pieces[0] | ((m_pieces[1] & 0x01) << 4);
    const int seconds = m_pieces[2] | ((m_pieces[3] & 0x03) << 4);
    const int minutes = m_pieces[4] | ((m_pieces[5] & 0x03) << 4);
    const int hours = m_pieces[6] | ((m_pieces[7] & 0x01) << 4);
    // The time is that of the frame when piece 0 was sent; piece 7 comes
    // 1¾ frames later.
    const double frameMs = 1000.0 / framesPerSecond(m_rate);
    const qint64 now = timecodeToMs(hours, minutes, seconds, frames, m_rate) + qint64(std::llround(1.75 * frameMs));
    // Following on, or did it jump (more than a few frames from where the
    // running clock says it should be)?
    const std::optional<qint64> expected = position(nowMs);
    if (!expected || !m_running || std::llabs(*expected - now) > qint64(4 * frameMs))
        m_jumped = true;
    m_position = now;
    m_positionAt = nowMs;
    m_running = true;
}

std::optional<qint64> MtcDecoder::position(qint64 nowMs) const
{
    if (!m_position)
        return std::nullopt;
    if (!running(nowMs))
        return m_running && m_lastQuarter >= 0 ? *m_position + (m_lastQuarter - m_positionAt) : *m_position;
    return *m_position + (nowMs - m_positionAt);
}

bool MtcDecoder::running(qint64 nowMs) const
{
    return m_running && m_lastQuarter >= 0 && nowMs - m_lastQuarter <= stopAfterMs;
}

bool MtcDecoder::takeJump()
{
    const bool jumped = m_jumped;
    m_jumped = false;
    return jumped;
}

} // namespace lumen::core::show
