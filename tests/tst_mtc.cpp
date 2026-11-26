#include "lumen/core/show/timecode.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <tuple>

#include <vector>

using namespace lumen::core::show;

// MIDI timecode: timecode arithmetic (including drop-frame) and the decoder
// that turns quarter-frame and full-frame messages into a position.
namespace {

struct Sent
{
    std::vector<unsigned char> bytes;
    qint64 atMs;
};

// The eight quarter frames for hh:mm:ss:ff, starting at startMs and spaced a
// quarter frame apart, as a sender would send them.
std::vector<Sent> quarterFrames(int h, int m, int s, int f, MtcRate rate, double startMs)
{
    const double quarterMs = 1000.0 / framesPerSecond(rate) / 4.0;
    const int values[8] = {f & 0x0F, (f >> 4) & 0x01, s & 0x0F, (s >> 4) & 0x03,
                           m & 0x0F, (m >> 4) & 0x03, h & 0x0F, ((h >> 4) & 0x01) | (int(rate) << 1)};
    std::vector<Sent> out;
    for (int piece = 0; piece < 8; ++piece)
        out.push_back({{0xF1, static_cast<unsigned char>((piece << 4) | values[piece])}, qint64(startMs + piece * quarterMs)});
    return out;
}

bool feedAll(MtcDecoder &decoder, const std::vector<Sent> &messages)
{
    bool any = false;
    for (const Sent &m : messages)
        any |= decoder.feed(m.bytes.data(), m.bytes.size(), m.atMs);
    return any;
}

} // namespace

class TestMtc : public QObject
{
    Q_OBJECT

private slots:
    void timecodeToMilliseconds()
    {
        QCOMPARE(timecodeToMs(1, 0, 0, 0, MtcRate::Fps25), 3600000);
        QCOMPARE(timecodeToMs(0, 0, 1, 12, MtcRate::Fps24), 1500);
        QCOMPARE(timecodeToMs(0, 0, 0, 15, MtcRate::Fps30), 500);
        // Drop-frame: 00:01:00;02 is frame 1800 (labels ;00 and ;01 skipped).
        QCOMPARE(timecodeToMs(0, 1, 0, 2, MtcRate::Fps2997Drop), 60060);
        // An hour of drop-frame labels is an hour of real time (to the frame).
        QVERIFY(std::abs(timecodeToMs(1, 0, 0, 0, MtcRate::Fps2997Drop) - 3600000) <= 34);
    }

    void formatsAndParses()
    {
        QCOMPARE(formatTimecode(3600000, MtcRate::Fps25), QStringLiteral("01:00:00:00"));
        QCOMPARE(formatTimecode(1500, MtcRate::Fps24), QStringLiteral("00:00:01:12"));
        for (const auto &[h, m, s, f] : std::vector<std::tuple<int, int, int, int>>{
                 {0, 0, 59, 29}, {0, 1, 0, 2}, {0, 9, 59, 29}, {0, 10, 0, 0}, {1, 23, 45, 17}}) {
            const QString label = QStringLiteral("%1:%2:%3;%4")
                                      .arg(h, 2, 10, QLatin1Char('0'))
                                      .arg(m, 2, 10, QLatin1Char('0'))
                                      .arg(s, 2, 10, QLatin1Char('0'))
                                      .arg(f, 2, 10, QLatin1Char('0'));
            QCOMPARE(formatTimecode(timecodeToMs(h, m, s, f, MtcRate::Fps2997Drop), MtcRate::Fps2997Drop), label);
        }
        QCOMPARE(parseTimecode(QStringLiteral("01:00:00:00"), MtcRate::Fps25), std::optional<qint64>(3600000));
        QCOMPARE(parseTimecode(QStringLiteral(" 00:00:10;15 "), MtcRate::Fps30), std::optional<qint64>(10500));
        QVERIFY(!parseTimecode(QStringLiteral("1:00"), MtcRate::Fps25));
        QVERIFY(!parseTimecode(QStringLiteral("00:00:00:25"), MtcRate::Fps25)); // no frame 25 at 25 fps
        QVERIFY(!parseTimecode(QStringLiteral("00:61:00:00"), MtcRate::Fps25));
    }

    // Eight quarter frames give a position: the time sent, plus the 1¾ frames
    // the eight messages took.
    void quarterFramesGiveAPosition()
    {
        for (const MtcRate rate : {MtcRate::Fps24, MtcRate::Fps25, MtcRate::Fps2997Drop, MtcRate::Fps30}) {
            MtcDecoder decoder;
            QVERIFY(!decoder.position(0));
            const auto sent = quarterFrames(1, 2, 3, 4, rate, 1000);
            QVERIFY(feedAll(decoder, sent));
            QCOMPARE(decoder.rate(), rate);
            QVERIFY(decoder.running(sent.back().atMs));
            const double frameMs = 1000.0 / framesPerSecond(rate);
            const qint64 expected = timecodeToMs(1, 2, 3, 4, rate) + qint64(std::llround(1.75 * frameMs));
            QCOMPARE(*decoder.position(sent.back().atMs), expected);
            // Between messages the position moves on with the clock.
            QCOMPARE(*decoder.position(sent.back().atMs + 20), expected + 20);
            QVERIFY(decoder.takeJump()); // the first position is a jump
            QVERIFY(!decoder.takeJump());
        }
    }

    // Running timecode follows on without jumps; when it stops, the position
    // stays where it stopped.
    void followsAndStops()
    {
        MtcDecoder decoder;
        const MtcRate rate = MtcRate::Fps25; // a frame is 40 ms, a run 80 ms
        qint64 t = 0;
        for (int frame = 0; frame < 50; frame += 2) {
            feedAll(decoder, quarterFrames(1, 0, frame / 25, frame % 25, rate, double(t)));
            t += 80;
        }
        QVERIFY(decoder.takeJump()); // the start
        QVERIFY(!decoder.takeJump()); // then smooth
        const qint64 last = t - 80 + 70; // the last quarter frame
        QVERIFY(decoder.running(last + 100));
        const qint64 at = *decoder.position(last);
        QCOMPARE(at, timecodeToMs(1, 0, 1, 23, rate) + 70); // the last run sent 01:00:01:23, 1¾ frames ago
        // Silence: stopped after stopAfterMs, frozen at the last message.
        QVERIFY(!decoder.running(last + MtcDecoder::stopAfterMs + 1));
        QCOMPARE(*decoder.position(last + 5000), at);
    }

    void jumpsAreNoticed()
    {
        MtcDecoder decoder;
        feedAll(decoder, quarterFrames(1, 0, 0, 0, MtcRate::Fps25, 0));
        feedAll(decoder, quarterFrames(1, 0, 0, 2, MtcRate::Fps25, 80));
        QVERIFY(decoder.takeJump());
        QVERIFY(!decoder.takeJump());
        feedAll(decoder, quarterFrames(1, 5, 0, 0, MtcRate::Fps25, 160)); // the sender located
        QVERIFY(decoder.takeJump());
        QCOMPARE(*decoder.position(230), timecodeToMs(1, 5, 0, 0, MtcRate::Fps25) + 70);
    }

    // A full-frame message: where the sender jumped to, not running.
    void fullFrameLocates()
    {
        MtcDecoder decoder;
        const unsigned char locate[] = {0xF0, 0x7F, 0x7F, 0x01, 0x01, (1 << 5) | 0, 5, 0, 0, 0xF7}; // 25 fps, 00:05:00:00
        QVERIFY(decoder.feed(locate, sizeof locate, 500));
        QCOMPARE(decoder.rate(), MtcRate::Fps25);
        QCOMPARE(*decoder.position(500), 300000);
        QCOMPARE(*decoder.position(9000), 300000);
        QVERIFY(!decoder.running(500));
        QVERIFY(decoder.takeJump());
    }

    // Pieces out of order (playing backwards, a lost message) give nothing
    // until a clean run from piece 0.
    void outOfOrderIsIgnored()
    {
        MtcDecoder decoder;
        auto sent = quarterFrames(1, 0, 0, 0, MtcRate::Fps25, 0);
        std::reverse(sent.begin(), sent.end());
        QVERIFY(!feedAll(decoder, sent));
        QVERIFY(!decoder.position(100));
        auto lossy = quarterFrames(1, 0, 0, 0, MtcRate::Fps25, 200);
        lossy.erase(lossy.begin() + 3);
        QVERIFY(!feedAll(decoder, lossy));
        QVERIFY(feedAll(decoder, quarterFrames(1, 0, 0, 10, MtcRate::Fps25, 300)));
        QVERIFY(decoder.position(370));
        // Other MIDI messages are not timecode.
        const unsigned char note[] = {0x90, 60, 100};
        QVERIFY(!decoder.feed(note, sizeof note, 400));
    }
};

QTEST_GUILESS_MAIN(TestMtc)
#include "tst_mtc.moc"
