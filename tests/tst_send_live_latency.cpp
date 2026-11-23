#include "operatorharness.h"

#include "lumen/core/render/renderlink.h"
#include "lumen/core/show/outputbridge.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QTest>
#include <QUuid>

#include <algorithm>

using namespace lumen;
using namespace lumen::core::render;
using namespace lumen::core::show;
using lumen::test::OperatorHarness;

// Batch 5 "Done when": clicking a slide or pressing Space sends it live in
// under 50 ms.
//
// The real operator window drives a real lumen-render process. Each sample is
// timed from the input event to the renderer reporting that it has presented
// a frame of the resulting scene (status "presentedSequence"). The median and
// 95th percentile of 20 clicks and of 20 Space presses must both be under
// 50 ms.
class TestSendLiveLatency : public QObject
{
    Q_OBJECT

private:
    static constexpr double limitMs = 50.0;
    static constexpr int samples = 20;

    std::unique_ptr<OperatorHarness> m_ui;
    std::unique_ptr<RenderLink> m_link;
    std::unique_ptr<OutputBridge> m_bridge;

    // Waits until the renderer has presented a frame of the given scene.
    // Returns the elapsed time, or -1 on timeout.
    double waitForPresented(quint64 sequence, const QElapsedTimer &since)
    {
        QDeadlineTimer deadline(2000);
        while (!deadline.hasExpired()) {
            auto status = m_link->request({{"cmd", "status"}}, 1000);
            if (status.ok()
                && quint64(status.value().value(QStringLiteral("presentedSequence")).toInteger()) >= sequence)
                return double(since.nsecsElapsed()) / 1e6;
        }
        return -1;
    }

    static void report(const char *what, std::vector<double> times, double *median, double *p95)
    {
        std::sort(times.begin(), times.end());
        *median = times[times.size() / 2];
        *p95 = times[std::size_t(std::ceil(0.95 * double(times.size()))) - 1];
        qInfo("%s: median %.1f ms, 95th percentile %.1f ms, fastest %.1f ms, slowest %.1f ms", what,
              *median, *p95, times.front(), times.back());
    }

private slots:
    void init()
    {
        m_ui = std::make_unique<OperatorHarness>();
        QString error;
        QVERIFY2(m_ui->load(false, &error), qPrintable(error));

        RenderLink::Options options;
        options.serverName = QStringLiteral("lumen-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        options.executable = QStringLiteral(LUMEN_RENDER_PATH);
        options.arguments = {QStringLiteral("--windowed")};
        m_link = std::make_unique<RenderLink>(options);
        auto started = m_link->start();
        QVERIFY2(started.ok(), started.ok() ? "" : qPrintable(started.error().message));
        m_bridge = std::make_unique<OutputBridge>(m_ui->show, *m_link);
        QVERIFY(m_bridge->attach().ok());

        m_ui->click("planItem-2"); // Amazing Grace, 9 slides
        QTRY_VERIFY(m_ui->item("slide-8"));
        // Let both windows settle before measuring.
        QTest::qWait(200);
    }

    void cleanup()
    {
        if (m_link)
            (void)m_link->quit();
        m_bridge.reset();
        m_link.reset();
        m_ui.reset();
    }

    void clickSendsSlideLiveUnder50ms()
    {
        std::vector<double> times;
        for (int i = 0; i < samples; ++i) {
            const QString card = QStringLiteral("slide-%1").arg((i + 1) % 9);
            const quint64 before = m_ui->show.scene().sequence;
            QElapsedTimer timer;
            timer.start();
            QVERIFY(m_ui->click(card));
            const quint64 sequence = m_ui->show.scene().sequence;
            QVERIFY2(sequence > before, "the click did not change the scene");
            const double ms = waitForPresented(sequence, timer);
            QVERIFY2(ms >= 0, "the renderer never presented the slide");
            times.push_back(ms);
            QTest::qWait(30); // let the operator window repaint, as between real clicks
        }
        double median = 0, p95 = 0;
        report("Click to screen", times, &median, &p95);
        QVERIFY2(median < limitMs, qPrintable(QStringLiteral("median %1 ms").arg(median)));
        QVERIFY2(p95 < limitMs, qPrintable(QStringLiteral("95th percentile %1 ms").arg(p95)));
    }

    void spaceSendsSlideLiveUnder50ms()
    {
        std::vector<double> times;
        m_ui->controller->goLive(0);
        while (int(times.size()) < samples) {
            if (m_ui->controller->previewSlide() < 0) {
                m_ui->controller->goLive(0); // back to the top; not measured
                QTest::qWait(30);
            }
            const quint64 before = m_ui->show.scene().sequence;
            QElapsedTimer timer;
            timer.start();
            m_ui->key(Qt::Key_Space);
            const quint64 sequence = m_ui->show.scene().sequence;
            QVERIFY2(sequence > before, "Space did not change the scene");
            const double ms = waitForPresented(sequence, timer);
            QVERIFY2(ms >= 0, "the renderer never presented the slide");
            times.push_back(ms);
            QTest::qWait(30);
        }
        double median = 0, p95 = 0;
        report("Space to screen", times, &median, &p95);
        QVERIFY2(median < limitMs, qPrintable(QStringLiteral("median %1 ms").arg(median)));
        QVERIFY2(p95 < limitMs, qPrintable(QStringLiteral("95th percentile %1 ms").arg(p95)));
    }
};

QTEST_MAIN(TestSendLiveLatency)
#include "tst_send_live_latency.moc"
