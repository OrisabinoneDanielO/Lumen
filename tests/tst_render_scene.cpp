#include "outputwindow.h"
#include "rendercheck.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>

using lumen::render::OutputWindow;
using namespace lumen::test;

// The output scene draws a colour and text, and keeps presenting frames.
class TestRenderScene : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    }

    void startsBlackAndEmpty()
    {
        OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(480, 270));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        const QImage frame = output.grab();
        QCOMPARE(frame.size(), QSize(480, 270) * output.window()->devicePixelRatio());
        QVERIFY(backgroundIs(frame, Qt::black));
        QCOMPARE(textPixels(frame), 0);
    }

    void drawsColourAndText()
    {
        OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(480, 270));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));

        const QColor navy(0x14, 0x21, 0x3D);
        output.setContent(navy, QStringLiteral("Amazing grace! How sweet the sound"));
        QTRY_VERIFY(backgroundIs(output.grab(), navy));
        QVERIFY2(textPixels(output.grab()) > 100, "the text should be drawn in white");

        // Clearing the text leaves only the colour.
        output.setContent(navy, QString());
        QTRY_COMPARE(textPixels(output.grab()), 0);
        QVERIFY(backgroundIs(output.grab(), navy));
    }

    void drawsYorubaDiacritics()
    {
        OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(480, 270));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        output.setContent(Qt::black, QStringLiteral("Ẹ jẹ́ ká yin Olúwa"));
        QTRY_VERIFY(textPixels(output.grab()) > 100);
    }

    void keepsPresentingFrames()
    {
        OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(320, 180));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        QTRY_VERIFY(output.framesPresented() > 0);
        const quint64 first = output.framesPresented();
        // Nothing changes in the scene, yet frames keep coming (~60 a second).
        QTest::qWait(500);
        QVERIFY2(output.framesPresented() >= first + 10,
                 qPrintable(QStringLiteral("only %1 frames in 500 ms")
                                .arg(output.framesPresented() - first)));
    }
};

QTEST_MAIN(TestRenderScene)
#include "tst_render_scene.moc"
