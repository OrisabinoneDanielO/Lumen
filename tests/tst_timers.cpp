#include "operatorharness.h"
#include "outputwindow.h"
#include "rendercheck.h"

#include "lumen/core/show/scene.h"

#include <QDateTime>
#include <QQuickItem>
#include <QSignalSpy>

using namespace lumen;
using namespace lumen::app;
using namespace lumen::core::show;

// Batch 12: timers and basic messages.
namespace {

// Texts of every visible item with this name under `item`.
QStringList texts(QQuickItem *item, const QString &name)
{
    QStringList found;
    if (!item || !item->isVisible())
        return found;
    if (item->objectName() == name && !item->property("text").toString().isEmpty())
        found << item->property("text").toString();
    for (QQuickItem *child : item->childItems())
        found << texts(child, name);
    return found;
}

int rowOf(QObject *model, const QString &name)
{
    auto *rows = qobject_cast<RowModel *>(model);
    for (int i = 0; i < rows->rowCount(); ++i) {
        if (rows->get(i).value("name").toString() == name)
            return i;
    }
    return -1;
}

} // namespace

class TestTimers : public QObject
{
    Q_OBJECT

private slots:
    void timerText_data()
    {
        QTest::addColumn<QJsonObject>("timer");
        QTest::addColumn<qint64>("now");
        QTest::addColumn<QString>("shown");
        const QJsonObject five{{"mode", "countdown"}, {"endsAtMs", 300000}};
        QTest::newRow("countdown start") << five << qint64(0) << "5:00";
        QTest::newRow("half a second in") << five << qint64(500) << "5:00";
        QTest::newRow("one second in") << five << qint64(1000) << "4:59";
        QTest::newRow("last moment") << five << qint64(299999) << "0:01";
        QTest::newRow("at zero") << five << qint64(300000) << "0:00";
        QTest::newRow("past zero") << five << qint64(400000) << "0:00";
        QTest::newRow("paused") << QJsonObject{{"mode", "countdown"}, {"pausedMs", 90000}} << qint64(123456) << "1:30";
        QTest::newRow("elapsed") << QJsonObject{{"mode", "elapsed"}, {"startedAtMs", 1000}} << qint64(62900) << "1:01";
        QTest::newRow("hours") << QJsonObject{{"mode", "elapsed"}, {"startedAtMs", 0}} << qint64(3725000) << "1:02:05";
        QTest::newRow("to a time") << QJsonObject{{"mode", "countdownToTime"}, {"endsAtMs", 7200000}} << qint64(0)
                                   << "2:00:00";
    }

    void timerText()
    {
        QFETCH(QJsonObject, timer);
        QFETCH(qint64, now);
        QFETCH(QString, shown);
        QCOMPARE(core::show::timerText(timer, now), shown);
    }

    void timerContentTravels()
    {
        Scene scene;
        scene.sequence = 3;
        scene.layer(Layer::Messages).content =
            Content::timerContent(QJsonObject{{"id", "t1"}, {"mode", "countdown"}, {"endsAtMs", 1000}}, "Sermon");
        const auto back = sceneFromJson(toJson(scene));
        QVERIFY(back.has_value());
        QCOMPARE(*back, scene);
        // An unknown mode is refused, like any malformed content.
        QJsonObject json = toJson(scene);
        QJsonArray layers = json.value("layers").toArray();
        QJsonObject messages = layers[int(Layer::Messages)].toObject();
        QJsonObject content = messages.value("content").toObject();
        content.insert("timer", QJsonObject{{"mode", "sideways"}});
        messages.insert("content", content);
        layers[int(Layer::Messages)] = messages;
        json.insert("layers", layers);
        QVERIFY(!sceneFromJson(json).has_value());
    }

    // The render process counts by itself: no new scene, yet the time moves.
    void rendererCountsOnItsOwn()
    {
        render::OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(480, 270));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        output.setManualClock(true);
        const qint64 wall = 1'800'000'000'000;
        output.setWallClock(wall);

        Scene scene;
        scene.sequence = 1;
        scene.layer(Layer::Slide).content = Content::timerContent(
            QJsonObject{{"id", "t"}, {"mode", "countdown"}, {"endsAtMs", wall + 5 * 60 * 1000}}, "Service starts soon");
        scene.layer(Layer::Messages).content =
            Content::timerContent(QJsonObject{{"id", "e"}, {"mode", "elapsed"}, {"startedAtMs", wall}}, "Sermon");
        output.setScene(scene);
        QImage frame = output.grab();
        QVERIFY(texts(output.window()->contentItem(), "timerText").contains("5:00"));
        QVERIFY(texts(output.window()->contentItem(), "timerBanner").contains("Sermon   0:00"));
        QVERIFY(test::textPixelsBetween(frame, 0.0, 0.12) > 20); // the banner
        QVERIFY(test::textPixelsBetween(frame, 0.4, 0.8) > 50);  // the large time

        output.advanceClock(61 * 1000);
        frame = output.grab();
        QVERIFY(texts(output.window()->contentItem(), "timerText").contains("3:59"));
        QVERIFY(texts(output.window()->contentItem(), "timerBanner").contains("Sermon   1:01"));
    }

    void startPauseReset()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        qint64 now = 1'800'000'000'000;
        ui.timers->setClock([&now] { return now; });
        const int sermon = rowOf(ui.timers->timers(), "Sermon");
        const int elapsed = rowOf(ui.timers->timers(), "Elapsed");
        QVERIFY(sermon >= 0 && elapsed >= 0 && rowOf(ui.timers->timers(), "Service start") >= 0);

        QVERIFY(ui.timers->show(sermon));
        const auto banner = [&] { return ui.show.scene().layer(Layer::Messages).content; };
        QCOMPARE(banner().kind, Content::Kind::Timer);
        QCOMPARE(banner().text, QStringLiteral("Sermon"));
        QCOMPARE(core::show::timerText(banner().timer, now), QStringLiteral("30:00")); // not started

        QVERIFY(ui.timers->start(sermon));
        QVERIFY(!ui.timers->start(sermon)); // already running
        // The output was told when the countdown ends.
        QCOMPARE(banner().timer.value("endsAtMs").toInteger(), now + 30 * 60 * 1000);
        now += 90 * 1000;
        QCOMPARE(core::show::timerText(banner().timer, now), QStringLiteral("28:30"));
        QVERIFY(ui.timers->pause(sermon));
        now += 60 * 1000; // paused: no change
        QCOMPARE(core::show::timerText(banner().timer, now), QStringLiteral("28:30"));
        QVERIFY(ui.timers->start(sermon));
        now += 30 * 1000;
        QCOMPARE(core::show::timerText(banner().timer, now), QStringLiteral("28:00"));
        QVERIFY(ui.timers->reset(sermon));
        QCOMPARE(core::show::timerText(banner().timer, now), QStringLiteral("30:00"));

        // Counting up.
        QVERIFY(ui.timers->start(elapsed));
        now += 75 * 1000;
        QTRY_VERIFY(ui.timers->summary().startsWith(QStringLiteral("Elapsed 1:15")));
        ui.timers->hide();
        QVERIFY(ui.show.scene().layer(Layer::Messages).content.isEmpty());

        // Adding and removing.
        QVERIFY(!ui.timers->add(QStringLiteral("Bad"), QStringLiteral("countdown"), QStringLiteral("soon")));
        QVERIFY(!ui.timers->add(QStringLiteral("Bad"), QStringLiteral("countdownToTime"), QStringLiteral("25:99")));
        QVERIFY(ui.timers->add(QStringLiteral("Announcements"), QStringLiteral("countdown"), QStringLiteral("5")));
        QVERIFY(ui.timers->add(QStringLiteral("Second service"), QStringLiteral("countdownToTime"), QStringLiteral("11:30")));
        const int added = rowOf(ui.timers->timers(), "Announcements");
        QVERIFY(added >= 0);
        QCOMPARE(qobject_cast<RowModel *>(ui.timers->timers())->get(added).value("display").toString(), QStringLiteral("5:00"));
        QVERIFY(ui.timers->remove(added));
        QCOMPARE(rowOf(ui.timers->timers(), "Announcements"), -1);
    }

    // "Pre-service countdown" has a timer element: live, it counts down to
    // the service start time, and the operator's Live frame shows it too.
    void countdownSlide()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        ui.controller->selectItem(0);
        QCOMPARE(ui.controller->itemTitle(), QStringLiteral("Pre-service countdown"));
        QVERIFY(ui.controller->goLive(0));
        const Content live = ui.show.scene().layer(Layer::Slide).content;
        QCOMPARE(live.kind, Content::Kind::Timer);
        QCOMPARE(live.text, QStringLiteral("Service starts soon"));
        QCOMPARE(live.timer.value("mode").toString(), QStringLiteral("countdownToTime"));
        const QDateTime target = QDateTime::fromMSecsSinceEpoch(live.timer.value("endsAtMs").toInteger());
        QCOMPARE(target.time(), QTime(10, 0));
        QCOMPARE(target.date(), QDate::currentDate());
        QVERIFY(!ui.controller->liveTimerText().isEmpty());
        QCOMPARE(ui.controller->liveText(), QStringLiteral("Service starts soon"));
        // Visible timers in the Preview and Live frames.
        const auto frames = [&](QQuickItem *item, auto &&self) -> int {
            int shown = item->objectName() == QLatin1String("frameTimer") && item->isVisible() ? 1 : 0;
            for (QQuickItem *child : item->childItems())
                shown += self(child, self);
            return shown;
        };
        QTRY_VERIFY(frames(ui.window->contentItem(), frames) >= 1); // Live shows it

        // A song next: no timer left in the frames.
        ui.controller->selectItem(2);
        QVERIFY(ui.controller->goLive(0));
        QVERIFY(ui.controller->liveTimerText().isEmpty());
        QTRY_COMPARE(frames(ui.window->contentItem(), frames), 0);
    }

    void messages()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        ui.controller->selectItem(2);
        QVERIFY(ui.controller->goLive(0));
        const Content slide = ui.show.scene().layer(Layer::Slide).content;

        QVERIFY(ui.click("messageButton"));
        QVERIFY(ui.item("messagePopover")->isVisible());
        ui.settle();
        for (const char c : QByteArrayLiteral("Parent of 42, please come to the nursery"))
            QTest::keyClick(ui.window, c);
        QTest::keyClick(ui.window, Qt::Key_Return);
        const Content message = ui.show.scene().layer(Layer::Messages).content;
        QCOMPARE(message.kind, Content::Kind::Text);
        QCOMPARE(message.text, QStringLiteral("Parent of 42, please come to the nursery"));
        QCOMPARE(ui.show.scene().layer(Layer::Slide).content, slide); // the slide is untouched
        QCOMPARE(ui.controller->recentMessages().front(), message.text);

        QVERIFY(ui.click("messageHide"));
        QVERIFY(ui.show.scene().layer(Layer::Messages).content.isEmpty());
        QVERIFY(ui.click("recentMessage-0")); // shown again with a click
        QCOMPARE(ui.show.scene().layer(Layer::Messages).content.text, message.text);
        QVERIFY(!ui.show.scene().layer(Layer::Messages).content.style.value("ticker").toBool());
        // With Ticker on, a recent message scrolls too (it was always a banner).
        QVERIFY(ui.click("messageTicker"));
        QVERIFY(ui.click("messageHide"));
        QVERIFY(ui.click("recentMessage-0"));
        QVERIFY(ui.show.scene().layer(Layer::Messages).content.style.value("ticker").toBool());
        // Clear Msg (F4) clears it too.
        QVERIFY(ui.click("clearMessages"));
        QVERIFY(ui.show.scene().layer(Layer::Messages).content.isEmpty());
    }

    void timersPopover()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QVERIFY(ui.click("timersButton"));
        ui.settle();
        const int sermon = rowOf(ui.timers->timers(), "Sermon");
        QVERIFY(ui.click(QStringLiteral("timerStart-%1").arg(sermon)));
        QVERIFY(qobject_cast<RowModel *>(ui.timers->timers())->get(sermon).value("running").toBool());
        QTRY_VERIFY(ui.timers->summary().startsWith(QStringLiteral("Sermon 30:00"))
                    || ui.timers->summary().startsWith(QStringLiteral("Sermon 29:5")));
        QTRY_COMPARE(ui.item("timersButton")->property("text").toString(), ui.timers->summary());
        QVERIFY(ui.click(QStringLiteral("timerShow-%1").arg(sermon)));
        QCOMPARE(ui.show.scene().layer(Layer::Messages).content.kind, Content::Kind::Timer);
        QVERIFY(ui.click(QStringLiteral("timerStart-%1").arg(sermon))); // now Pause
        QVERIFY(ui.show.scene().layer(Layer::Messages).content.timer.contains("pausedMs"));

        // Keyboard only: a count up needs just a name, so Enter adds it; a
        // countdown's name goes on to the minutes.
        QVERIFY(ui.click("timerMode-elapsed"));
        QVERIFY(ui.click("timerName"));
        for (const char c : QByteArrayLiteral("Worship"))
            QTest::keyClick(ui.window, c);
        QTest::keyClick(ui.window, Qt::Key_Return);
        QTRY_VERIFY(rowOf(ui.timers->timers(), "Worship") >= 0);
        QVERIFY(ui.click("timerMode-countdown"));
        QVERIFY(ui.click("timerName"));
        for (const char c : QByteArrayLiteral("Offering"))
            QTest::keyClick(ui.window, c);
        QTest::keyClick(ui.window, Qt::Key_Return);
        for (const char c : QByteArrayLiteral("5"))
            QTest::keyClick(ui.window, c);
        QTest::keyClick(ui.window, Qt::Key_Return);
        QTRY_VERIFY(rowOf(ui.timers->timers(), "Offering") >= 0);
    }

    void outputLightTogglesOutput()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QSignalSpy toggled(ui.controller.get(), &OperatorController::outputToggleRequested);
        ui.controller->toggleOutput();
        QCOMPARE(toggled.takeFirst().first().toBool(), true); // off, so: turn on
        ui.controller->setOutputConnected(true);
        ui.controller->toggleOutput();
        QCOMPARE(toggled.takeFirst().first().toBool(), false);
    }
};

QTEST_MAIN(TestTimers)
#include "tst_timers.moc"
