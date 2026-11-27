#include "outputwindow.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using lumen::render::OutputWindow;
using namespace lumen::core::show;

// Video in the render process (batch 9): plays, loops, is counted.
class TestVideoPlayback : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<OutputWindow> m_output;
    quint64 m_sequence = 0;

    void show(const Content &background, const Content &media = {})
    {
        Scene scene;
        scene.sequence = ++m_sequence;
        scene.layer(Layer::Background).content = background;
        scene.layer(Layer::Media).content = media;
        m_output->setScene(scene);
    }

private slots:
    void init()
    {
        m_output = std::make_unique<OutputWindow>();
        QVERIFY(m_output->create());
        m_output->place(QGuiApplication::primaryScreen(), false, QSize(960, 540));
        QVERIFY(QTest::qWaitForWindowExposed(m_output->window()));
    }
    void cleanup() { m_output.reset(); }

    // A video that cannot be played (missing, or not a video) is reported in
    // the renderer's status, and forgotten once it is off the screen.
    void unplayableVideoIsReported()
    {
        QTemporaryDir dir;
        const QString broken = dir.filePath(QStringLiteral("broken.mp4"));
        QFile file(broken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not a video");
        file.close();
        show(Content::fill(Qt::black), Content::video(broken, false, 0.0));
        QTRY_VERIFY_WITH_TIMEOUT(!m_output->videoErrors().isEmpty(), 5000);
        const QJsonObject error = m_output->videoErrors().first().toObject();
        QCOMPARE(error.value("path").toString(), broken);
        QVERIFY(!error.value("message").toString().isEmpty());
        show(Content::fill(Qt::black)); // off the screen
        QVERIFY(m_output->videoErrors().isEmpty());
    }

    // Trim in and out points; pause, play and seek from the operator; the
    // position reported back.
    void trimPauseAndSeek()
    {
        const QString file = QStringLiteral(LUMEN_MEDIA_DIR "/loop-1080p-vp9.webm"); // 10 s
        const auto position = [&] { return m_output->videoPosition().value("positionMs").toInteger(); };
        const auto playing = [&] { return m_output->videoPosition().value("playing").toBool(); };

        Content clip = Content::video(file, false, 0.0);
        clip.trimInMs = 2000;
        clip.trimOutMs = 4000;
        show(Content::fill(Qt::black), clip);
        // Starts at the in point, plays, stops at the out point.
        QTRY_VERIFY_WITH_TIMEOUT(playing() && position() >= 2000 && position() < 3500, 5000);
        QCOMPARE(m_output->videoPosition().value("path").toString(), file);
        QCOMPARE(m_output->videoPosition().value("durationMs").toInteger(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(!playing() && position() >= 3900, 5000);
        QTest::qWait(500);
        QVERIFY(position() < 4400); // stopped there

        // Seek back and play; pause holds it still.
        m_output->videoControl(QStringLiteral("seek"), 2500);
        m_output->videoControl(QStringLiteral("play"), 0);
        QTRY_VERIFY_WITH_TIMEOUT(playing() && position() >= 2500 && position() < 3500, 3000);
        m_output->videoControl(QStringLiteral("pause"), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!playing(), 2000);
        QTest::qWait(300);
        const qint64 held = position();
        QTest::qWait(700);
        QVERIFY(qAbs(position() - held) < 100);

        // A trimmed loop goes round between its points.
        clip.loop = true;
        clip.trimOutMs = 3000;
        show(Content::fill(Qt::black), clip);
        m_output->videoControl(QStringLiteral("play"), 0);
        QTRY_VERIFY_WITH_TIMEOUT(playing(), 3000);
        for (int i = 0; i < 12; ++i) {
            QTest::qWait(250);
            QVERIFY2(position() >= 1900 && position() <= 3200, qPrintable(QString::number(position())));
        }
        QVERIFY(playing());

        // Off the screen: nothing to report.
        show(Content::fill(Qt::black));
        QTRY_VERIFY(m_output->videoPosition().isEmpty());
    }

    void motionLoopPlaysAndLoops()
    {
        const int seconds = qEnvironmentVariableIntValue("LUMEN_VIDEO_SECONDS") > 0
                                ? qEnvironmentVariableIntValue("LUMEN_VIDEO_SECONDS") : 12;
        show(Content::video(QStringLiteral(LUMEN_MEDIA_DIR "/loop-1080p-vp9.webm"), true, 0.0));
        QTRY_VERIFY_WITH_TIMEOUT(m_output->videoStats().frames > 0, 5000);
        QTest::qWait(seconds * 1000);
        const auto s = m_output->videoStats();
        qInfo("frames %llu dropped %llu loops %llu stalls %llu gpu %llu maxGap %.1f ms start %lld ms",
              s.frames, s.dropped, s.loops, s.stalls, s.gpuFrames, s.maxGapMs, s.startMs);
        QVERIFY(s.frames >= quint64(seconds * 30 * 0.9));
        QVERIFY(s.loops >= quint64(seconds / 10));
        QCOMPARE(s.dropped, quint64(0));
    }
};

QTEST_MAIN(TestVideoPlayback)
#include "tst_video_playback.moc"
