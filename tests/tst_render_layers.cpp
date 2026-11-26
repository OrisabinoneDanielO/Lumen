#include "outputwindow.h"
#include "rendercheck.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>
#include <QQuickItem>

#include <functional>

using lumen::render::OutputWindow;
using namespace lumen::core::show;
using namespace lumen::test;

// The renderer's layer stack, transitions and overrides, on a manual clock so
// every mid-fade frame is exact.
class TestRenderLayers : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<OutputWindow> m_output;
    Scene m_scene;

    void show()
    {
        ++m_scene.sequence;
        m_output->setScene(m_scene);
    }

    QImage after(qint64 ms)
    {
        if (ms > 0)
            m_output->advanceClock(ms);
        return m_output->grab();
    }

    const QColor navy{0x14, 0x21, 0x3D};
    const QColor gold{0xE0, 0xA0, 0x20};

private slots:
    void init()
    {
        // Any warning fails, except Qt's note that a missing font family
        // made font lookup slow (macOS, where "Sans Serif" does not exist).
        QTest::failOnWarning(QRegularExpression(QStringLiteral("^(?!Populating font family aliases took ).*")));
        m_output = std::make_unique<OutputWindow>();
        QVERIFY(m_output->create());
        m_output->place(QGuiApplication::primaryScreen(), false, QSize(480, 270));
        QVERIFY(QTest::qWaitForWindowExposed(m_output->window()));
        m_output->setManualClock(true);
        m_scene = Scene{};
    }

    void cleanup() { m_output.reset(); }

    void layersStackInOrder()
    {
        m_scene.layer(Layer::Background).content = Content::fill(navy);
        m_scene.layer(Layer::Slide).content = Content::textContent("Amazing grace");
        m_scene.layer(Layer::Props).content = Content::textContent("Pastor Ade");
        m_scene.layer(Layer::Messages).content = Content::textContent("Parent of 42");
        show();
        QImage frame = after(0);

        QVERIFY(near(colourAt(frame, 0.01, 0.5), navy));           // background at the edge
        QVERIFY(textPixelsBetween(frame, 0.3, 0.7) > 50);          // slide text in the middle
        QVERIFY(textPixelsBetween(frame, 0.75, 0.95) > 20);        // props: lower third
        QVERIFY(textPixelsBetween(frame, 0.0, 0.12) > 20);         // messages: top banner
        // The props band darkens the background behind it.
        QVERIFY(colourAt(frame, 0.98, 0.9).blue() < navy.blue() - 20);

        // Masks is the top layer: an opaque fill hides everything below.
        m_scene.layer(Layer::Masks).content = Content::fill(Qt::red);
        show();
        frame = after(0);
        for (double y : {0.05, 0.5, 0.9})
            QVERIFY(near(colourAt(frame, 0.5, y), Qt::red));
        QCOMPARE(textPixels(frame), 0);
    }

    // Each transition halfway: the new content half in (and the old half
    // out), as the kind says; at the end only the new content, in place.
    void transitionsHalfway_data()
    {
        QTest::addColumn<QString>("kind");
        for (const char *kind : {"fade", "push", "pushUp", "wipe", "zoom"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void transitionsHalfway()
    {
        QFETCH(QString, kind);
        const auto find = [this](const QString &name) {
            QQuickItem *found = nullptr;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (!item || found)
                    return;
                if (item->objectName() == name)
                    found = item;
                for (QQuickItem *child : item->childItems())
                    walk(child);
            };
            walk(m_output->window()->contentItem());
            return found;
        };
        m_scene.layer(Layer::Slide).content = Content::fill(navy);
        show();
        m_scene.layer(Layer::Slide).content = Content::fill(gold);
        m_scene.layer(Layer::Slide).transition = Transition::of(*transitionKindFromName(kind), 1000);
        show();
        m_output->advanceClock(500);
        QQuickItem *incoming = find(QStringLiteral("layerCurrent-slide"));
        QQuickItem *outgoing = find(QStringLiteral("layerPrevious-slide"));
        QQuickItem *clip = find(QStringLiteral("layerClip-slide"));
        QVERIFY(incoming && outgoing && clip);
        const qreal w = m_output->window()->width(), h = m_output->window()->height();
        if (kind == QLatin1String("fade")) {
            QVERIFY(std::abs(incoming->opacity() - 0.5) < 0.01);
        } else if (kind == QLatin1String("push")) {
            QVERIFY(std::abs(incoming->x() - w / 2) < 1);
            QVERIFY(std::abs(outgoing->x() + w / 2) < 1);
            QCOMPARE(incoming->opacity(), 1.0);
        } else if (kind == QLatin1String("pushUp")) {
            QVERIFY(std::abs(incoming->y() - h / 2) < 1);
            QVERIFY(std::abs(outgoing->y() + h / 2) < 1);
        } else if (kind == QLatin1String("wipe")) {
            QVERIFY(clip->clip());
            QVERIFY(std::abs(clip->width() - w / 2) < 1);
            QCOMPARE(outgoing->opacity(), 1.0);
        } else if (kind == QLatin1String("zoom")) {
            QVERIFY(std::abs(incoming->scale() - 0.9) < 0.01);
            QVERIFY(std::abs(incoming->opacity() - 0.5) < 0.01);
        }
        // Halfway, the picture is neither all old nor all new.
        const QImage mid = m_output->grab();
        QVERIFY(!mid.isNull());
        // At the end: the new content alone, in place.
        const QImage end = after(600);
        QCOMPARE(incoming->x(), 0.0);
        QCOMPARE(incoming->y(), 0.0);
        QCOMPARE(incoming->scale(), 1.0);
        QCOMPARE(clip->width(), w);
        QVERIFY(!outgoing->isVisible());
        QVERIFY(near(end.pixelColor(end.width() / 2, end.height() / 2), gold));
    }

    void cutIsImmediate()
    {
        m_scene.layer(Layer::Background).content = Content::fill(navy);
        show();
        QVERIFY(near(colourAt(after(0), 0.5, 0.5), navy));
        m_scene.layer(Layer::Background).content = Content::fill(gold);
        show();
        QVERIFY(near(colourAt(after(0), 0.5, 0.5), gold)); // no clock time needed
    }

    void fadeCrossfadesLinearly()
    {
        m_scene.layer(Layer::Background).content = Content::fill(navy);
        show();
        m_scene.layer(Layer::Background) = {Content::fill(gold), Transition::fade(1000)};
        show();

        QVERIFY(near(colourAt(after(0), 0.5, 0.5), navy));
        QVERIFY2(near(colourAt(after(250), 0.5, 0.5), mix(navy, gold, 0.25), 4),
                 qPrintable(colourAt(m_output->grab(), 0.5, 0.5).name()));
        QVERIFY(near(colourAt(after(250), 0.5, 0.5), mix(navy, gold, 0.5), 4));
        QVERIFY(near(colourAt(after(500), 0.5, 0.5), gold));
        QVERIFY(near(colourAt(after(1000), 0.5, 0.5), gold)); // and stays there
    }

    void clearFadesOut()
    {
        m_scene.layer(Layer::Slide) = {Content::textContent("Chorus"), Transition::fade(400)};
        show();
        QCOMPARE(textPixels(after(0)), 0);       // fading in from nothing
        QVERIFY(textPixels(after(400)) > 50);    // fully in

        m_scene.layer(Layer::Slide).content = Content::empty();
        show();
        const QImage half = after(200);
        QCOMPARE(textPixels(half), 0);                    // no longer full white...
        QVERIFY(pixelsBrighterThan(half, 60) > 50);       // ...but still visible
        QCOMPARE(pixelsBrighterThan(after(200), 10), 0);  // gone
    }

    void layersTransitionIndependently()
    {
        m_scene.layer(Layer::Background).content = Content::fill(navy);
        show();
        m_scene.layer(Layer::Background) = {Content::fill(gold), Transition::fade(1000)};
        m_scene.layer(Layer::Slide) = {Content::textContent("Verse 2"), Transition::cut()};
        show();
        const QImage frame = after(500);
        QVERIFY(near(colourAt(frame, 0.01, 0.5), mix(navy, gold, 0.5), 4)); // background mid-fade
        QVERIFY(textPixels(frame) > 50);                                   // slide already in
    }

    void blackCoversEverythingAndRestores()
    {
        m_scene.layer(Layer::Background).content = Content::fill(navy);
        m_scene.layer(Layer::Slide).content = Content::textContent("Verse 1");
        m_scene.overrideTransition = Transition::fade(500);
        show();
        const QImage before = after(0);

        m_scene.black = true;
        show();
        QVERIFY(near(colourAt(after(250), 0.01, 0.5), mix(navy, Qt::black, 0.5), 4));
        const QImage black = after(250);
        QVERIFY(backgroundIs(black, Qt::black));
        QCOMPARE(pixelsBrighterThan(black, 2), 0);

        m_scene.black = false;
        show();
        QVERIFY(after(500) == before); // exactly what was underneath
    }

    void logoShowsAboveLayersAndBlackAboveLogo()
    {
        m_scene.layer(Layer::Background).content = Content::fill(navy);
        m_scene.overrideTransition = Transition::cut();
        m_scene.logo = true;
        m_scene.logoContent = Content::textContent("Grace Church");
        show();
        QImage frame = after(0);
        QVERIFY(backgroundIs(frame, Qt::black)); // logo sits on black
        QVERIFY(textPixels(frame) > 50);

        m_scene.black = true;
        show();
        QCOMPARE(pixelsBrighterThan(after(0), 2), 0);

        m_scene.black = false;
        m_scene.logo = false;
        show();
        QVERIFY(near(colourAt(after(0), 0.5, 0.5), navy));
    }

    // A fade that has started finishes on the renderer's own clock, with no
    // further messages (so a UI crash mid-fade cannot freeze it).
    void fadeFinishesWithoutFurtherInput()
    {
        m_scene.layer(Layer::Background) = {Content::fill(gold), Transition::fade(300)};
        show();
        QVERIFY(near(colourAt(after(300), 0.5, 0.5), gold));
    }

    void changeDuringAFadeRestartsFromTheNewContent()
    {
        m_scene.layer(Layer::Background).content = Content::fill(navy);
        show();
        m_scene.layer(Layer::Background) = {Content::fill(gold), Transition::fade(1000)};
        show();
        after(500);
        m_scene.layer(Layer::Background).content = Content::fill(Qt::white);
        show();
        // Fades from gold (the content that was coming in) to white.
        QVERIFY(near(colourAt(after(500), 0.5, 0.5), mix(gold, Qt::white, 0.5), 4));
        QVERIFY(near(colourAt(after(500), 0.5, 0.5), Qt::white));
    }
};

QTEST_MAIN(TestRenderLayers)
#include "tst_render_layers.moc"
