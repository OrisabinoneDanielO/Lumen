#include "lumen/core/show/scene.h"
#include "lumen/core/show/showcontroller.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

using namespace lumen::core::show;

Q_DECLARE_METATYPE(lumen::core::show::Scene)

// The show controller's rules and the scene's wire format.
class TestShowController : public QObject
{
    Q_OBJECT

private slots:
    void startsEmpty()
    {
        ShowController show;
        QCOMPARE(show.scene().sequence, quint64(0));
        for (const LayerState &layer : show.scene().layers)
            QVERIFY(layer.content.isEmpty());
        QVERIFY(!show.scene().logo);
        QVERIFY(!show.scene().black);
    }

    void everyChangeBumpsTheSequenceOnce()
    {
        ShowController show;
        QSignalSpy spy(&show, &ShowController::sceneChanged);
        show.setContent(Layer::Slide, Content::textContent("Verse 1"));
        show.setContent(Layer::Background, Content::fill(Qt::blue));
        show.setTransition(Layer::Slide, Transition::fade(500));
        QCOMPARE(spy.count(), 3);
        QCOMPARE(show.scene().sequence, quint64(3));
        QCOMPARE(spy.last().first().value<Scene>(), show.scene());
    }

    void noChangeEmitsNothing()
    {
        ShowController show;
        show.setContent(Layer::Slide, Content::textContent("Verse 1"));
        QSignalSpy spy(&show, &ShowController::sceneChanged);
        show.setContent(Layer::Slide, Content::textContent("Verse 1"));
        show.clear(Layer::Props); // already empty
        show.setBlack(false);     // already off
        QCOMPARE(spy.count(), 0);
        QCOMPARE(show.scene().sequence, quint64(1));
    }

    void clearOneLayerLeavesTheOthers()
    {
        ShowController show;
        show.setContent(Layer::Background, Content::fill(Qt::blue));
        show.setContent(Layer::Slide, Content::textContent("Chorus"));
        show.setContent(Layer::Props, Content::textContent("Pastor James"));
        show.clear(Layer::Slide);
        QVERIFY(show.scene().layer(Layer::Slide).content.isEmpty());
        QCOMPARE(show.scene().layer(Layer::Background).content, Content::fill(Qt::blue));
        QCOMPARE(show.scene().layer(Layer::Props).content, Content::textContent("Pastor James"));
    }

    void clearAllEmptiesLayersButKeepsOverridesAndTransitions()
    {
        ShowController show;
        for (int i = 0; i < layerCount; ++i)
            show.setContent(Layer(i), Content::fill(QColor::fromHsv(i * 40, 200, 200)));
        show.setTransition(Layer::Slide, Transition::fade(700));
        show.setBlack(true);
        QSignalSpy spy(&show, &ShowController::sceneChanged);
        show.clearAll();
        QCOMPARE(spy.count(), 1); // one change, one snapshot
        for (const LayerState &layer : show.scene().layers)
            QVERIFY(layer.content.isEmpty());
        QVERIFY(show.scene().black);
        QCOMPARE(show.scene().layer(Layer::Slide).transition, Transition::fade(700));
    }

    // Black and Logo sit above the layers; turning them off reveals the
    // layers as they are now, including changes made underneath.
    void overridesKeepTheLayersUnderneath()
    {
        ShowController show;
        show.setContent(Layer::Slide, Content::textContent("Verse 1"));
        show.toggleBlack();
        QVERIFY(show.scene().black);
        show.setContent(Layer::Slide, Content::textContent("Verse 2")); // prepared under Black
        show.toggleBlack();
        QVERIFY(!show.scene().black);
        QCOMPARE(show.scene().layer(Layer::Slide).content, Content::textContent("Verse 2"));

        show.setLogo(true);
        show.setBlack(true);
        QVERIFY(show.scene().logo && show.scene().black); // independent
        show.setBlack(false);
        QVERIFY(show.scene().logo);
        show.toggleLogo();
        QVERIFY(!show.scene().logo);
    }

    void adoptContinuesTheSequenceWithoutEmitting()
    {
        Scene onAir;
        onAir.sequence = 41;
        onAir.layer(Layer::Slide).content = Content::textContent("Still on air");
        ShowController show;
        QSignalSpy spy(&show, &ShowController::sceneChanged);
        show.adopt(onAir);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(show.scene(), onAir);
        show.clear(Layer::Slide);
        QCOMPARE(show.scene().sequence, quint64(42));
    }

    void sceneRoundTripsThroughJson()
    {
        Scene scene;
        scene.sequence = 123456789012;
        scene.layer(Layer::Background) = {Content::fill(QColor(0x14, 0x21, 0x3D)), Transition::fade(1000)};
        scene.layer(Layer::Media) = {Content::image("/media/Ìjọsìn.png"), Transition::cut()};
        scene.layer(Layer::Slide) = {Content::textContent("Ẹ jẹ́ ká yin Olúwa", QColor(255, 255, 0)), Transition::fade(250)};
        scene.layer(Layer::Masks) = {Content::fill(QColor(255, 0, 0, 128)), Transition::cut()};
        scene.logo = true;
        scene.logoContent = Content::image("/media/logo.png");
        scene.overrideTransition = Transition::cut();

        const auto back = sceneFromJson(toJson(scene));
        QVERIFY(back);
        QCOMPARE(*back, scene);
    }

    void malformedScenesAreRejected()
    {
        const QJsonObject good = toJson(Scene{});
        QVERIFY(sceneFromJson(good));

        QJsonObject noLayers = good;
        noLayers.remove("layers");
        QVERIFY(!sceneFromJson(noLayers));

        QJsonObject negative = good;
        negative.insert("sequence", -1);
        QVERIFY(!sceneFromJson(negative));

        QJsonObject badKind = good;
        QJsonArray layers = badKind.value("layers").toArray();
        layers[2] = QJsonObject{{"content", QJsonObject{{"kind", "hologram"}}}};
        badKind.insert("layers", layers);
        QVERIFY(!sceneFromJson(badKind));

        QJsonObject badTransition = good;
        layers = badTransition.value("layers").toArray();
        layers[0] = QJsonObject{{"transition", QJsonObject{{"kind", "spin"}}}};
        badTransition.insert("layers", layers);
        QVERIFY(!sceneFromJson(badTransition));
    }

    void layerNames()
    {
        QCOMPARE(layerName(Layer::Background), QStringLiteral("background"));
        QCOMPARE(layerName(Layer::Masks), QStringLiteral("masks"));
        for (int i = 0; i < layerCount; ++i)
            QCOMPARE(layerFromName(layerName(Layer(i))), std::optional<Layer>(Layer(i)));
        QVERIFY(!layerFromName("stage"));
    }
};

QTEST_GUILESS_MAIN(TestShowController)
#include "tst_show_controller.moc"
