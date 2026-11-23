#include "operatorharness.h"
#include "outputwindow.h"
#include "rendercheck.h"

#include <QUrl>

#include <functional>

using namespace lumen;
using namespace lumen::app;
using namespace lumen::core::show;

// Batch 14 "Done when": props persist across slides and clear independently.
// Also: lower thirds and bugs drawn by the renderer, and messages with fields
// (templates) shown as a banner or a ticker.
namespace {

int rowOf(QObject *model, const QString &name)
{
    auto *rows = qobject_cast<RowModel *>(model);
    for (int i = 0; i < rows->rowCount(); ++i) {
        if (rows->get(i).value("name").toString() == name)
            return i;
    }
    return -1;
}

const QString logo = QStringLiteral(LUMEN_MEDIA_DIR "/bars-720p.png");

} // namespace

class TestProps : public QObject
{
    Q_OBJECT

private slots:
    void propsPersistAndClearIndependently()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        PropsController &props = *ui.props;
        QVERIFY(props.addLowerThird(QStringLiteral("Pastor Ade Johnson"), QStringLiteral("Lead pastor")));
        QVERIFY(!props.addLowerThird(QStringLiteral("  "), QStringLiteral("no name")));
        QVERIFY(props.addBug(QUrl::fromLocalFile(logo), QStringLiteral("topRight")));
        QVERIFY(!props.addBug(QUrl::fromLocalFile(QStringLiteral("/no/such/logo.png")), QStringLiteral("topRight")));
        const int lower = rowOf(props.props(), "Pastor Ade Johnson");
        const int bug = rowOf(props.props(), "bars-720p");
        QVERIFY(lower >= 0 && bug >= 0);

        QVERIFY(props.show(lower));
        QVERIFY(props.show(bug));
        const auto propsLayer = [&] { return ui.show.scene().layer(Layer::Props).content; };
        const Content up = propsLayer();
        QCOMPARE(up.kind, Content::Kind::Prop);
        QCOMPARE(up.prop.value("lowerThird").toObject().value("line1").toString(), QStringLiteral("Pastor Ade Johnson"));
        QVERIFY(up.prop.value("bug").toObject().value("image").toString().startsWith(QStringLiteral("file://")));
        QVERIFY(qobject_cast<RowModel *>(props.props())->get(lower).value("shown").toBool());

        // Through a service: every slide of several items, a video, a
        // message, a timer banner, Clear Slide, Logo and Black.
        auto *plan = qobject_cast<RowModel *>(ui.controller->plan());
        for (int item = 0; item < plan->rowCount(); ++item) {
            ui.controller->selectItem(item);
            for (int slide = 0; slide < ui.controller->slides()->property("count").toInt(); ++slide) {
                QVERIFY(ui.controller->goLive(slide));
                QCOMPARE(propsLayer(), up);
            }
        }
        QVERIFY(ui.controller->showMessage(QStringLiteral("Parent of 42")));
        QVERIFY(ui.timers->show(0));
        ui.controller->clearLayer(QStringLiteral("slide"));
        ui.controller->clearLayer(QStringLiteral("messages"));
        ui.controller->clearLayer(QStringLiteral("background"));
        ui.controller->toggleLogo();
        ui.controller->toggleLogo();
        ui.controller->toggleBlack();
        ui.controller->toggleBlack();
        QCOMPARE(propsLayer(), up);

        // Independently: the bug comes down, the lower third stays.
        ui.controller->selectItem(2);
        QVERIFY(ui.controller->goLive(0));
        const Content slide = ui.show.scene().layer(Layer::Slide).content;
        QVERIFY(props.hide(bug));
        QVERIFY(propsLayer().prop.contains("lowerThird"));
        QVERIFY(!propsLayer().prop.contains("bug"));
        QVERIFY(props.show(bug));
        QVERIFY(props.hide(lower));
        QVERIFY(!propsLayer().prop.contains("lowerThird"));
        QVERIFY(propsLayer().prop.contains("bug"));
        QVERIFY(props.hide(bug));
        QVERIFY(propsLayer().isEmpty());
        QCOMPARE(ui.show.scene().layer(Layer::Slide).content, slide); // untouched

        // F3 clears props only; the slide stays.
        QVERIFY(props.show(lower));
        QVERIFY(props.show(bug));
        QVERIFY(ui.controller->handleKey(Qt::Key_F3, 0));
        QVERIFY(propsLayer().isEmpty());
        QCOMPARE(ui.show.scene().layer(Layer::Slide).content, slide);
        QTRY_VERIFY(!qobject_cast<RowModel *>(props.props())->get(lower).value("shown").toBool());

        // Clear All takes props down with everything else.
        QVERIFY(props.show(lower));
        ui.controller->clearAll();
        QVERIFY(propsLayer().isEmpty());

        // Removing a prop that is up takes it off the screen.
        QVERIFY(props.show(lower));
        QVERIFY(props.remove(lower));
        QVERIFY(propsLayer().isEmpty());
    }

    void rendererDrawsProps()
    {
        render::OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(960, 540));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        output.setManualClock(true);
        Scene scene;
        scene.sequence = 1;
        scene.layer(Layer::Background).content = Content::fill(QColor(0x14, 0x21, 0x3D));
        scene.layer(Layer::Props).content = Content::propContent(QJsonObject{
            {"lowerThird", QJsonObject{{"id", "a"}, {"line1", "Pastor Ade Johnson"}, {"line2", "Lead pastor"}}},
            {"bug", QJsonObject{{"id", "b"}, {"image", QUrl::fromLocalFile(logo).toString()}, {"corner", "topRight"}, {"size", 0.12}}}});
        output.setScene(scene);
        QTRY_VERIFY(output.visibleTexts("lowerThird").value("lowerThirdLine1").toString() == QLatin1String("Pastor Ade Johnson"));
        QCOMPARE(output.visibleTexts("lowerThird").value("lowerThirdLine2").toString(), QStringLiteral("Lead pastor"));
        QImage frame;
        QTRY_VERIFY((frame = output.grab(), !test::near(test::colourAt(frame, 0.93, 0.1), QColor(0x14, 0x21, 0x3D), 20)));
        // LUMEN_PROPS_DUMP=<file.png> keeps the frame, to look at by eye.
        if (const QString dump = qEnvironmentVariable("LUMEN_PROPS_DUMP"); !dump.isEmpty())
            frame.save(dump);
        // The lower third band is dark over the navy background, with text.
        QVERIFY(test::textPixelsBetween(frame, 0.74, 0.9) > 50);
        // Bottom right and top left stay background.
        QVERIFY(test::near(test::colourAt(frame, 0.97, 0.95), QColor(0x14, 0x21, 0x3D), 3));
        QVERIFY(test::near(test::colourAt(frame, 0.03, 0.05), QColor(0x14, 0x21, 0x3D), 3));
    }

    void tickerScrolls()
    {
        render::OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(960, 540));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        Scene scene;
        scene.sequence = 1;
        Content message = Content::textContent(QStringLiteral("Parent of 42, please come to the nursery"));
        message.style = QJsonObject{{"ticker", true}};
        scene.layer(Layer::Messages).content = message;
        output.setScene(scene);
        // The visible ticker (each layer has an outgoing and an incoming copy).
        std::function<QQuickItem *(QQuickItem *)> find = [&](QQuickItem *item) -> QQuickItem * {
            if (!item || !item->isVisible())
                return nullptr;
            if (item->objectName() == QLatin1String("tickerText") && !item->property("text").toString().isEmpty())
                return item;
            for (QQuickItem *child : item->childItems()) {
                if (QQuickItem *found = find(child))
                    return found;
            }
            return nullptr;
        };
        QQuickItem *ticker = nullptr;
        QTRY_VERIFY((ticker = find(output.window()->contentItem())) != nullptr);
        const qreal first = ticker->x();
        QTRY_VERIFY(ticker->x() < first - 20); // moving left
    }

    void templatesWithFields()
    {
        QCOMPARE(MessageTemplates::fieldsOf(QStringLiteral("Parent of {number}, please come to {room}; {number}")),
                 (QStringList{"number", "room"}));
        QCOMPARE(MessageTemplates::fill(QStringLiteral("Parent of {number}, please come to the nursery"),
                                        {{"number", " 42 "}}),
                 QStringLiteral("Parent of 42, please come to the nursery"));
        QVERIFY(MessageTemplates::fill(QStringLiteral("Parent of {number}"), {}).isEmpty()); // a field left empty

        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        const int nursery = rowOf(ui.templates->templates(), "Nursery");
        QVERIFY(nursery >= 0); // the three built-in ones
        QCOMPARE(ui.templates->fields(nursery), QStringList{"number"});
        QVERIFY(!ui.templates->show(nursery, {}, false));
        QVERIFY(ui.templates->show(nursery, {{"number", "42"}}, true));
        const Content shown = ui.show.scene().layer(Layer::Messages).content;
        QCOMPARE(shown.text, QStringLiteral("Parent of 42, please come to the nursery"));
        QCOMPARE(shown.style.value("ticker").toBool(), true);

        QVERIFY(ui.templates->add(QStringLiteral("Room"), QStringLiteral("{group} meets in room {room}")));
        const int room = rowOf(ui.templates->templates(), "Room");
        QCOMPARE(ui.templates->fields(room), (QStringList{"group", "room"}));
        QVERIFY(ui.templates->remove(room));
        QCOMPARE(rowOf(ui.templates->templates(), "Room"), -1);

        // In the Message popover: pick the template, fill the field, Show.
        ui.controller->hideMessage();
        QVERIFY(ui.click("messageButton"));
        ui.settle();
        QVERIFY(ui.click(QStringLiteral("messageTemplate-%1").arg(nursery)));
        ui.settle();
        QVERIFY(ui.click("messageField-0"));
        for (const char c : QByteArrayLiteral("17"))
            QTest::keyClick(ui.window, c);
        QVERIFY(ui.click("messageShow"));
        QCOMPARE(ui.show.scene().layer(Layer::Messages).content.text, QStringLiteral("Parent of 17, please come to the nursery"));
        QVERIFY(!ui.show.scene().layer(Layer::Messages).content.style.value("ticker").toBool());
    }

    void propsPopover()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QVERIFY(ui.click("propsButton"));
        ui.settle();
        QVERIFY(ui.click("propLine1"));
        for (const char c : QByteArrayLiteral("Sister Ngozi"))
            QTest::keyClick(ui.window, c);
        QVERIFY(ui.click("propLine2"));
        for (const char c : QByteArrayLiteral("Choir director"))
            QTest::keyClick(ui.window, c);
        QVERIFY(ui.click("propAddLowerThird"));
        ui.settle();
        const int row = rowOf(ui.props->props(), "Sister Ngozi");
        QVERIFY(row >= 0);
        QVERIFY(ui.click(QStringLiteral("propRow-%1").arg(row)));
        QCOMPARE(ui.show.scene().layer(Layer::Props).content.prop.value("lowerThird").toObject().value("line2").toString(),
                 QStringLiteral("Choir director"));
        QVERIFY(ui.click(QStringLiteral("propRow-%1").arg(row))); // and down again
        QVERIFY(ui.show.scene().layer(Layer::Props).content.isEmpty());
    }
};

QTEST_MAIN(TestProps)
#include "tst_props.moc"
