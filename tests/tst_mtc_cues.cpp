#include "outputwindow.h"

#include "lumen/core/show/scene.h"
#include "lumen/core/show/timecode.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTest>

#include <cmath>
#include <functional>

#ifdef LUMEN_HAVE_MIDI
#include "RtMidi.h"
#endif

using namespace lumen;
using namespace lumen::core::show;

// Timecode cues that follow MIDI timecode from another device: cues come up
// on the frame their timecode is reached, stop when the timecode stops, and
// go back when the sender locates back.
namespace {

QJsonObject mtcTimeline(const QString &id, bool testFeed, const QString &device = QString())
{
    QJsonArray cues;
    for (const auto &[ms, text] : QList<std::pair<int, QString>>{{0, "One"}, {1000, "Two"}, {2500, "Three"}})
        cues.append(QJsonObject{{"ms", ms}, {"content", contentToJson(Content::textContent(text))}});
    return {{"id", id},          {"source", "mtc"}, {"mtcStart", "01:00:00:00"}, {"mtcDevice", device},
            {"playing", true},   {"cues", cues},    {"testFeed", testFeed}};
}

// The eight quarter frames of frame `frame` (counted from 00:00:00:00) at
// 25 fps, each with its piece number.
QList<QByteArray> quarterFrames(qint64 frame)
{
    const int f = int(frame % 25), s = int(frame / 25 % 60), m = int(frame / 1500 % 60), h = int(frame / 90000);
    const int values[8] = {f & 0x0F, (f >> 4) & 1, s & 0x0F, (s >> 4) & 3, m & 0x0F, (m >> 4) & 3, h & 0x0F,
                           ((h >> 4) & 1) | (int(MtcRate::Fps25) << 1)};
    QList<QByteArray> out;
    for (int piece = 0; piece < 8; ++piece)
    {
        QByteArray message(2, 0);
        message[0] = char(0xF1);
        message[1] = char((piece << 4) | values[piece]);
        out << message;
    }
    return out;
}

QString onScreen(render::OutputWindow &output)
{
    QString text;
    std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
        if (!item || !item->isVisible())
            return;
        if (item->metaObject()->className() == QByteArray("QQuickText") && !item->property("text").toString().isEmpty()
            && item->opacity() > 0)
            text = item->property("text").toString();
        for (QQuickItem *child : item->childItems())
            find(child);
    };
    find(output.window()->contentItem());
    return text;
}

} // namespace

class TestMtcCues : public QObject
{
    Q_OBJECT

private slots:
    // Timecode from 00:59:59:00 at 25 fps, frames at 60 fps, on a clock that
    // moves a millisecond at a time: each cue comes up on the first frame at
    // or after its timecode.
    void cuesFollowTheTimecode()
    {
        render::OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(480, 270));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        output.setManualClock(true);
        Scene scene;
        scene.sequence = 1;
        scene.layer(Layer::Slide).content = Content::cued(mtcTimeline(QStringLiteral("mtc-1"), true));
        output.setScene(scene);
        QCOMPARE(output.timelineStatus().value("audio").toString(), QStringLiteral("timecode-waiting"));
        QCOMPARE(onScreen(output), QString()); // nothing before the timecode starts

        const qint64 firstFrame = 3599 * 25; // 00:59:59:00
        const double screenFrame = 1000.0 / 60.0;
        QHash<QString, qint64> firstSeen;
        QString previous;
        for (qint64 t = 0; t <= 4000; ++t) {
            // A quarter frame every 10 ms; a run of eight starts on even frames.
            if (t % 10 == 0) {
                const qint64 quarter = t / 10;
                const qint64 run = quarter / 8;
                output.feedMidi(quarterFrames(firstFrame + 2 * run).at(int(quarter % 8)));
            }
            output.advanceClock(1);
            if (std::fmod(double(t), screenFrame) < 1.0) {
                const QString text = onScreen(output);
                if (text != previous) {
                    firstSeen.insert(text, t);
                    previous = text;
                }
            }
        }
        QCOMPARE(output.timelineStatus().value("audio").toString(), QStringLiteral("timecode"));
        // 00:59:59:00 + 4 s.
        QVERIFY2(output.timelineStatus().value("timecode").toString().startsWith(QStringLiteral("01:00:03:")),
                 qPrintable(output.timelineStatus().value("timecode").toString()));
        // Clock t is timecode 00:59:59:00 + t, so cue ms is due at t = 1000 + ms.
        for (const auto &[ms, text] : QList<std::pair<int, QString>>{{0, "One"}, {1000, "Two"}, {2500, "Three"}}) {
            QVERIFY2(firstSeen.contains(text), qPrintable(text));
            const qint64 late = firstSeen.value(text) - (1000 + ms);
            QVERIFY2(late >= -2 && late < screenFrame + 5,
                     qPrintable(QStringLiteral("%1 %2 ms from its timecode").arg(text).arg(late)));
        }

        // The timecode stops: so does the timeline.
        output.advanceClock(500);
        QCOMPARE(output.timelineStatus().value("audio").toString(), QStringLiteral("timecode-stopped"));
        const qint64 stoppedAt = output.timelineStatus().value("positionMs").toInteger();
        output.advanceClock(3000);
        QCOMPARE(output.timelineStatus().value("positionMs").toInteger(), stoppedAt);
        QCOMPARE(onScreen(output), QStringLiteral("Three"));

        // The sender locates back to 01:00:00:10: the first cue again.
        const char locateBytes[] = {char(0xF0), char(0x7F), char(0x7F), char(0x01), char(0x01), char((1 << 5) | 1),
                                    0, 0, 10, char(0xF7)};
        const QByteArray locate(locateBytes, sizeof locateBytes);
        output.feedMidi(locate);
        output.advanceClock(17);
        QCOMPARE(output.timelineStatus().value("positionMs").toInteger(), 400);
        QCOMPARE(onScreen(output), QStringLiteral("One"));
    }

    // A real (virtual) MIDI device sending timecode in real time: the output
    // finds it by name and follows it.
    void followsAMidiDevice()
    {
#if !defined(LUMEN_HAVE_MIDI) || defined(Q_OS_WIN)
        QSKIP("needs virtual MIDI ports (not on Windows)");
#else
        std::unique_ptr<RtMidiOut> sender;
        try {
            sender = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "Test Timecode");
            sender->openVirtualPort("Test Timecode out");
        } catch (const RtMidiError &e) {
            QSKIP(qPrintable(QStringLiteral("no MIDI system here: ") + QString::fromStdString(e.getMessage())));
        }
        render::OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(480, 270));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        Scene scene;
        scene.sequence = 1;
        scene.layer(Layer::Slide).content = Content::cued(mtcTimeline(QStringLiteral("mtc-2"), false, QStringLiteral("Test Timecode")));
        output.setScene(scene);

        // 2.4 s of timecode from 00:59:59:20, in real time.
        QElapsedTimer clock;
        clock.start();
        const qint64 firstFrame = 3599 * 25 + 20;
        qint64 sent = 0;
        while (clock.elapsed() < 2400) {
            const qint64 due = clock.elapsed() / 10;
            for (; sent <= due; ++sent) {
                const QByteArray bytes = quarterFrames(firstFrame + 2 * (sent / 8)).at(int(sent % 8));
                std::vector<unsigned char> message(bytes.begin(), bytes.end());
                sender->sendMessage(&message);
            }
            QTest::qWait(2);
        }
        QTRY_COMPARE(onScreen(output), QStringLiteral("Two"));
        const QJsonObject status = output.timelineStatus();
        QCOMPARE(status.value("audio").toString(), QStringLiteral("timecode"));
        // 00:59:59:20 + 2.4 s: about 01:00:02:05, when the sender stopped.
        QVERIFY2(status.value("timecode").toString().startsWith(QStringLiteral("01:00:02:")),
                 qPrintable(status.value("timecode").toString()));
        for (const QJsonValue &s : status.value("switches").toArray()) {
            const qint64 late = s.toObject().value("atMs").toInteger() - s.toObject().value("cueMs").toInteger();
            QVERIFY2(late >= -10 && late < 120, qPrintable(QString::number(late)));
        }
#endif
    }
};

QTEST_MAIN(TestMtcCues)
#include "tst_mtc_cues.moc"
