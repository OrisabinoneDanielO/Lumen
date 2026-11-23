#include "outputwindow.h"

#include <QGuiApplication>
#include <QQuickWindow>
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
