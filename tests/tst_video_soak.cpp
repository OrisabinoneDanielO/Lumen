#include "support/timing.h"
#include "lumen/core/render/renderlink.h"
#include "lumen/core/show/scene.h"
#include "lumen/core/show/textstyle.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QSysInfo>
#include <QTest>
#include <QThread>
#include <QUuid>

using namespace lumen::core::render;
using namespace lumen::core::show;

// Batch 9 "Done when": a 2-hour loop plays with zero dropped frames on the
// reference PC (the developer's laptop: i5-1155G7, Iris Xe, VA-API).
//
// The real lumen-render process plays the 10-second 1080p VP9 loop as a
// motion background while the lyric slide above it changes every 10 seconds,
// as in a service. Every second the test reads the renderer's video
// statistics. At the end: no dropped frames, no visible stalls, every loop
// completed and the expected number of frames shown. Pauses are logged as they
// happen (with the renderer's own timing, to tell a held-up process from a
// slow decoder) and the run continues; a dropped frame ends it at once.
//
// LUMEN_SOAK_SECONDS sets the length (default 120 for routine runs; the
// reference run uses 7200). LUMEN_SOAK_SCREEN=<n> plays fullscreen on screen
// n, as a real output does; by default a window is used, and a window that
// is covered or on another workspace may stop being drawn (the compositor
// holds it), which shows up as pauses. LUMEN_REQUIRE_HW=1 also requires every frame to
// arrive as a GPU texture (hardware decode). LUMEN_SOAK_REPORT names a file
// to write the result to.
class TestVideoSoak : public QObject
{
    Q_OBJECT

private slots:
    void loopPlaysWithoutDroppedFrames()
    {
        LUMEN_TIMING_CHECK();
        const int seconds = qEnvironmentVariableIntValue("LUMEN_SOAK_SECONDS") > 0
                                ? qEnvironmentVariableIntValue("LUMEN_SOAK_SECONDS") : 120;
        const bool requireHardware = qEnvironmentVariableIntValue("LUMEN_REQUIRE_HW") == 1;

        RenderLink::Options options;
        options.serverName = QStringLiteral("lumen-soak-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        options.executable = QStringLiteral(LUMEN_RENDER_PATH);
        const QString screen = qEnvironmentVariable("LUMEN_SOAK_SCREEN");
        options.arguments = {screen.isEmpty() ? QStringLiteral("--windowed") : QStringLiteral("--screen=") + screen,
                             QStringLiteral("--test-mode"),
                             // If this test crashes or times out, the renderer stops too.
                             QStringLiteral("--watch-pid=%1").arg(QCoreApplication::applicationPid())};
        RenderLink link(options);
        auto started = link.start();
        QVERIFY2(started.ok(), started.ok() ? "" : qPrintable(started.error().message));

        QJsonObject timing; // the renderer's own main-loop and swap gaps
        auto status = [&link, &timing]() {
            auto reply = link.request({{"cmd", "status"}}, 5000);
            if (!reply.ok())
                return QJsonObject();
            timing = reply.value().value("timing").toObject();
            return reply.value().value("video").toObject();
        };
        Scene scene;
        auto send = [&](const QString &text) {
            ++scene.sequence;
            scene.layer(Layer::Slide).content = Content::styledText(text, QJsonObject{{"size", 64}, {"maxSize", 72}, {"minSize", 40}});
            QVERIFY(link.request({{"cmd", "scene"}, {"scene", toJson(scene)}}).ok());
        };
        scene.layer(Layer::Background).content =
            Content::video(QStringLiteral(LUMEN_MEDIA_DIR "/loop-1080p-vp9.webm"), true, 0.0);
        send(QStringLiteral("Amazing grace! How sweet the sound\nThat saved a wretch like me!"));

        QElapsedTimer waiting;
        waiting.start();
        while (status().value("frames").toInteger() == 0 && waiting.elapsed() < 10000)
            QThread::msleep(50);
        QVERIFY2(status().value("frames").toInteger() > 0, "the video never started");
        // Starting (opening the window, the first frames) is measured by
        // tst_video_start; the soak counts from 5 seconds after it.
        QTest::qWait(5000);

        const QStringList lyrics{QStringLiteral("I once was lost, but now am found;\nWas blind, but now I see."),
                                 QStringLiteral("'Twas grace that taught my heart to fear,\nAnd grace my fears relieved;"),
                                 QStringLiteral("How precious did that grace appear\nThe hour I first believed.")};
        QElapsedTimer clock;
        clock.start();
        // The elapsed timer stops while the computer sleeps; the wall clock
        // does not. A difference means the run was interrupted by sleep and
        // proves nothing (run with sleep blocked, e.g. systemd-inhibit).
        const QDateTime wallStart = QDateTime::currentDateTimeUtc();
        const auto slept = [&] {
            return wallStart.msecsTo(QDateTime::currentDateTimeUtc()) - clock.elapsed() > 5000;
        };
        const QJsonObject atStart = status();
        int slide = 0;
        qint64 nextSlide = 10000;
        qint64 nextReport = 60000;
        QJsonObject latest = atStart;
        qint64 stallsSeen = latest.value("stalls").toInteger();
        QStringList notes;
        while (clock.elapsed() < qint64(seconds) * 1000) {
            QThread::msleep(1000);
            latest = status();
            QVERIFY2(!slept(), "the computer went to sleep during the run; it does not count");
            QVERIFY2(!latest.isEmpty(), "the renderer stopped answering");
            if (clock.elapsed() >= nextSlide) {
                send(lyrics[slide++ % lyrics.size()]);
                nextSlide += 10000;
            }
            if (clock.elapsed() >= nextReport) {
                qInfo("%lld min: frames %lld dropped %lld loops %lld stalls %lld", clock.elapsed() / 60000,
                      latest.value("frames").toInteger(), latest.value("dropped").toInteger(),
                      latest.value("loops").toInteger(), latest.value("stalls").toInteger());
                nextReport += 60000;
            }
            // Note each pause as it happens, with whether the render process
            // itself was held up at the time, and keep playing: the full run
            // shows how often pauses happen and what they line up with.
            if (latest.value("stalls").toInteger() > stallsSeen) {
                stallsSeen = latest.value("stalls").toInteger();
                const QString note = QStringLiteral("%1 | pause at %2 s | stalls %3 | longest video gap %4 ms | "
                                                    "main loop longest gap %5 ms (%6 over 100 ms) | "
                                                    "swap longest gap %7 ms (%8 over 100 ms)")
                                         .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate))
                                         .arg(clock.elapsed() / 1000)
                                         .arg(stallsSeen)
                                         .arg(latest.value("maxGapMs").toDouble(), 0, 'f', 1)
                                         .arg(timing.value("tickMaxGapMs").toDouble(), 0, 'f', 1)
                                         .arg(timing.value("tickStalls").toInteger())
                                         .arg(timing.value("swapMaxGapMs").toDouble(), 0, 'f', 1)
                                         .arg(timing.value("swapStalls").toInteger());
                qInfo("%s", qPrintable(note));
                notes << note;
            }
            if (latest.value("dropped").toInteger() > atStart.value("dropped").toInteger())
                break; // fail fast with the numbers below
        }
        const double elapsed = clock.elapsed() / 1000.0;
        const qint64 frames = latest.value("frames").toInteger() - atStart.value("frames").toInteger();
        const qint64 dropped = latest.value("dropped").toInteger() - atStart.value("dropped").toInteger();
        const qint64 stalls = latest.value("stalls").toInteger() - atStart.value("stalls").toInteger();
        const qint64 loops = latest.value("loops").toInteger();
        const qint64 gpu = latest.value("gpuFrames").toInteger();
        const qint64 total = latest.value("frames").toInteger();
        const QString summary =
            QStringLiteral("%1 | %2 s | %3 | frames %4 (expected ~%5) | dropped %6 | stalls %7 | loops %8 | "
                           "GPU frames %9 of %10 | longest gap %11 ms | start %12 ms | "
                           "main loop pauses %13 | swap pauses %14")
                .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate))
                .arg(elapsed, 0, 'f', 0)
                .arg(QGuiApplication::platformName())
                .arg(frames)
                .arg(qint64(elapsed * 30))
                .arg(dropped)
                .arg(stalls)
                .arg(loops)
                .arg(gpu)
                .arg(total)
                .arg(latest.value("maxGapMs").toDouble(), 0, 'f', 1)
                .arg(latest.value("startMs").toInteger())
                .arg(timing.value("tickStalls").toInteger())
                .arg(timing.value("swapStalls").toInteger());
        qInfo("%s", qPrintable(summary));
        if (const QString report = qEnvironmentVariable("LUMEN_SOAK_REPORT"); !report.isEmpty()) {
            QFile file(report);
            if (file.open(QIODevice::Append | QIODevice::Text))
                file.write(((notes + QStringList{summary}).join(u'\n') + u'\n').toUtf8());
        }
        (void)link.quit();

        QCOMPARE(dropped, qint64(0));
        QCOMPARE(stalls, qint64(0));
        QVERIFY2(loops >= qint64(elapsed / 10.0) - 1, qPrintable(summary));
        QVERIFY2(std::abs(double(frames) - elapsed * 30.0) <= elapsed * 30.0 * 0.02 + 30, qPrintable(summary));
        if (requireHardware)
            QVERIFY2(gpu == total, "not every frame came from the hardware path");
    }
};

// Qt Test stops any test function after 5 minutes by default; the soak runs
// longer, so the limit is raised to its length plus a margin.
int main(int argc, char *argv[])
{
    const int seconds = qEnvironmentVariableIntValue("LUMEN_SOAK_SECONDS") > 0
                            ? qEnvironmentVariableIntValue("LUMEN_SOAK_SECONDS") : 120;
    if (!qEnvironmentVariableIsSet("QTEST_FUNCTION_TIMEOUT"))
        qputenv("QTEST_FUNCTION_TIMEOUT", QByteArray::number((qint64(seconds) + 120) * 1000));
    QGuiApplication app(argc, argv);
    TestVideoSoak test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_video_soak.moc"
