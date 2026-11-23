#include "outputwindow.h"
#include "rendercheck.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>

using lumen::render::OutputWindow;
using namespace lumen::core::show;
using namespace lumen::test;

namespace {

// Background colour plus centred slide text, both cuts (the batch 3 frame).
Scene colourAndText(const QColor &background, const QString &text, quint64 sequence = 1)
{
    Scene scene;
    scene.sequence = sequence;
    scene.layer(Layer::Background).content = Content::fill(background);
    if (!text.isEmpty())
        scene.layer(Layer::Slide).content = Content::textContent(text);
    return scene;
}

} // namespace

// The output scene draws a colour and text, and keeps presenting frames.
// (Batch 3 tests, moved from setContent() to scenes in batch 4.)
class TestRenderScene : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<OutputWindow> open(QSize size)
    {
        auto output = std::make_unique<OutputWindow>();
        if (!output->create())
            return nullptr;
        output->place(QGuiApplication::primaryScreen(), false, size);
        if (!QTest::qWaitForWindowExposed(output->window()))
            return nullptr;
        return output;
    }

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    }

    void startsBlackAndEmpty()
    {
        auto output = open(QSize(480, 270));
        QVERIFY(output);
        const QImage frame = output->grab();
        QCOMPARE(frame.size(), QSize(480, 270) * output->window()->devicePixelRatio());
        QVERIFY(backgroundIs(frame, Qt::black));
        QCOMPARE(textPixels(frame), 0);
    }

    void drawsColourAndText()
    {
        auto output = open(QSize(480, 270));
        QVERIFY(output);

        const QColor navy(0x14, 0x21, 0x3D);
        output->setScene(colourAndText(navy, QStringLiteral("Amazing grace! How sweet the sound")));
        QTRY_VERIFY(backgroundIs(output->grab(), navy));
        QVERIFY2(textPixels(output->grab()) > 100, "the text should be drawn in white");

        // Clearing the text leaves only the colour.
        output->setScene(colourAndText(navy, QString(), 2));
        QTRY_COMPARE(textPixels(output->grab()), 0);
        QVERIFY(backgroundIs(output->grab(), navy));
    }

    void drawsYorubaDiacritics()
    {
        auto output = open(QSize(480, 270));
        QVERIFY(output);
        output->setScene(colourAndText(Qt::black, QStringLiteral("Ẹ jẹ́ ká yin Olúwa")));
        QTRY_VERIFY(textPixels(output->grab()) > 100);
    }

    void keepsPresentingFrames()
    {
        auto output = open(QSize(320, 180));
        QVERIFY(output);
        QTRY_VERIFY(output->framesPresented() > 0);
        const quint64 first = output->framesPresented();
        // Nothing changes in the scene, yet frames keep coming (~60 a second).
        QTest::qWait(500);
        QVERIFY2(output->framesPresented() >= first + 10,
                 qPrintable(QStringLiteral("only %1 frames in 500 ms")
                                .arg(output->framesPresented() - first)));
    }
};

QTEST_MAIN(TestRenderScene)
#include "tst_render_scene.moc"
