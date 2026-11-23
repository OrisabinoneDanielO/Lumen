#include "operatorharness.h"
#include "outputwindow.h"
#include "rendercheck.h"

#include "lumen/core/render/renderlink.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QUuid>

using namespace lumen;
using namespace lumen::app;
using namespace lumen::core::render;

// Batch 13 "Done when": current, next, clock and message show on a second
// screen (the stage display).
namespace {

// 10:15 today, local time, as the renderer's wall clock.
qint64 quarterPastTen()
{
    return QDateTime(QDate::currentDate(), QTime(10, 15)).toMSecsSinceEpoch();
}

QJsonObject sampleState(qint64 wall, const QString &layout)
{
    return QJsonObject{
        {"layout", layout},
        {"item", "Amazing Grace"},
        {"detail", "Key of G"},
        {"group", "Verse 2"},
        {"now", "'Twas grace that taught my heart to fear,\nAnd grace my fears relieved;"},
        {"next", "How precious did that grace appear\nThe hour I first believed."},
        {"nextGroup", "Verse 3"},
        {"arrangement", QJsonArray{QJsonObject{{"label", "V1"}, {"current", false}},
                                   QJsonObject{{"label", "V2"}, {"current", true}},
                                   QJsonObject{{"label", "V3"}, {"current", false}}}},
        {"timer", QJsonObject{{"label", "Service timer"},
                              {"spec", QJsonObject{{"mode", "countdown"}, {"endsAtMs", wall + 5 * 60 * 1000}}}}},
        {"message", "Repeat the last verse"},
    };
}

} // namespace

class TestStage : public QObject
{
    Q_OBJECT

private slots:
    void layoutMatchesTheMockup()
    {
        render::OutputWindow stage(render::OutputWindow::Kind::Stage);
        QVERIFY(stage.create());
        stage.place(QGuiApplication::primaryScreen(), false, QSize(1280, 720));
        QVERIFY(QTest::qWaitForWindowExposed(stage.window()));
        stage.setManualClock(true);
        const qint64 wall = quarterPastTen();
        stage.setWallClock(wall);
        stage.setStage(sampleState(wall, "default"));
        QImage frame = stage.grab();
        // LUMEN_STAGE_DUMP=<file.png> keeps the frame, to compare with the mockup by eye.
        if (const QString dump = qEnvironmentVariable("LUMEN_STAGE_DUMP"); !dump.isEmpty())
            frame.save(dump);

        QVariantMap texts = stage.visibleTexts("stage");
        QCOMPARE(texts.value("stageGroup").toString(), QStringLiteral("VERSE 2"));
        QCOMPARE(texts.value("stageItem").toString(), QStringLiteral("Amazing Grace · Key of G"));
        QVERIFY(texts.value("stageNow").toString().startsWith(QStringLiteral("'Twas grace")));
        QVERIFY(texts.value("stageNext").toString().startsWith(QStringLiteral("How precious")));
        QCOMPARE(texts.value("stageClock").toString(), QStringLiteral("10:15"));
        QCOMPARE(texts.value("stageTimer").toString(), QStringLiteral("5:00"));
        QCOMPARE(texts.value("stageMessage").toString(), QStringLiteral("Repeat the last verse"));
        QCOMPARE(texts.value("stageChip-1").toString(), QStringLiteral("V2"));
        // Drawn: text on black, the message bar in its brown.
        QVERIFY(test::pixelsBrighterThan(frame, 200) > 500);
        QVERIFY(test::near(test::colourAt(frame, 0.5, 0.93), QColor(0x3A, 0x2A, 0x1E), 12));

        // The clock and timer run on the stage renderer's own clock.
        stage.advanceClock(65 * 1000);
        texts = stage.visibleTexts("stage");
        QCOMPARE(texts.value("stageClock").toString(), QStringLiteral("10:16"));
        QCOMPARE(texts.value("stageTimer").toString(), QStringLiteral("3:55"));

        // Lyrics: now and next only (and the message).
        stage.setStage(sampleState(wall, "lyrics"));
        texts = stage.visibleTexts("stage");
        QVERIFY(texts.contains("stageNow") && texts.contains("stageNext") && texts.contains("stageMessage"));
        QVERIFY(!texts.contains("stageClock") && !texts.contains("stageTimer") && !texts.contains("stageGroup"));

        // Clock: time and timer, now and the message; no next.
        stage.setStage(sampleState(wall, "clock"));
        texts = stage.visibleTexts("stage");
        QVERIFY(texts.contains("stageClock") && texts.contains("stageTimer") && texts.contains("stageNow"));
        QVERIFY(!texts.contains("stageNext"));

        // No message: no bar.
        QJsonObject quiet = sampleState(wall, "default");
        quiet.remove("message");
        stage.setStage(quiet);
        QVERIFY(!stage.visibleTexts("stage").contains("stageMessage"));
    }

    // What the operator does reaches the stage state.
    void stateFollowsTheOperator()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QList<QJsonObject> sent;
        ui.stage->setSender([&sent](const QJsonObject &state) { sent << state; });
        QVERIFY(sent.isEmpty()); // nothing until the stage is up
        ui.stage->setConnected(true);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.last().value("layout").toString(), QStringLiteral("default"));
        QCOMPARE(sent.last().value("timer").toObject().value("label").toString(), QStringLiteral("Service start"));

        ui.controller->selectItem(2); // Amazing Grace
        QCOMPARE(ui.controller->itemTitle(), QStringLiteral("Amazing Grace"));
        QVERIFY(ui.controller->goLive(2));
        QJsonObject state = sent.last();
        QCOMPARE(state.value("item").toString(), QStringLiteral("Amazing Grace"));
        QCOMPARE(state.value("detail").toString(), QStringLiteral("Key of G"));
        QCOMPARE(state.value("now").toString(), ui.controller->liveText());
        QCOMPARE(state.value("next").toString(), ui.controller->previewText());
        QVERIFY(!state.value("group").toString().isEmpty());
        const QJsonArray chips = state.value("arrangement").toArray();
        QVERIFY(chips.size() > 1);
        int current = 0;
        for (const QJsonValue &chip : chips)
            current += chip.toObject().value("current").toBool() ? 1 : 0;
        QCOMPARE(current, 1);

        // Notes from the plan show beside the key.
        QVERIFY(ui.controller->setItemNotes(2, QStringLiteral("Capo 2")));
        QVERIFY(ui.controller->goLive(3));
        QCOMPARE(sent.last().value("detail").toString(), QStringLiteral("Key of G · Capo 2"));

        // A message to the stage, a layout, a timer.
        const int before = int(sent.size());
        QVERIFY(ui.stage->showMessage(QStringLiteral("Repeat the last verse")));
        QCOMPARE(sent.last().value("message").toString(), QStringLiteral("Repeat the last verse"));
        QVERIFY(ui.show.scene().layer(core::show::Layer::Messages).content.isEmpty()); // not on the audience output
        ui.stage->setLayout(1);
        QCOMPARE(sent.last().value("layout").toString(), QStringLiteral("lyrics"));
        QVERIFY(ui.timers->start(1)); // Sermon
        QCOMPARE(sent.last().value("timer").toObject().value("label").toString(), QStringLiteral("Sermon"));
        QCOMPARE(int(sent.size()), before + 3);
        // Nothing new: nothing sent.
        ui.stage->setLayout(1);
        QCOMPARE(int(sent.size()), before + 3);

        // Clearing the slide clears NOW.
        ui.controller->clearLayer(QStringLiteral("slide"));
        QVERIFY(!sent.last().contains("now"));
    }

    void stagePopover()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QSignalSpy toggled(ui.stage.get(), &StageController::toggleRequested);
        QVERIFY(ui.click("output-1")); // the Stage light
        ui.settle();
        QVERIFY(ui.item("stagePopover") && ui.item("stageToggle"));
        QVERIFY(ui.click("stageToggle"));
        QCOMPARE(toggled.takeFirst().first().toBool(), true);
        QVERIFY(ui.click("stageLayout-2"));
        QCOMPARE(ui.stage->layout(), 2);
        QVERIFY(ui.click("stageMessageInput"));
        for (const char c : QByteArrayLiteral("Last chorus twice"))
            QTest::keyClick(ui.window, c);
        QVERIFY(ui.click("stageMessageSend"));
        QCOMPARE(ui.stage->message(), QStringLiteral("Last chorus twice"));
        QVERIFY(ui.click("stageMessageClear"));
        QVERIFY(ui.stage->message().isEmpty());
    }

    // Done when: a real stage renderer on the second of two screens shows
    // current, next, clock and message.
    void showsOnASecondScreen()
    {
        // Two virtual screens, side by side.
        QTemporaryFile config(QDir::temp().filePath(QStringLiteral("lumen-screens-XXXXXX.json")));
        QVERIFY(config.open());
        config.write(R"({"screens": [
            {"name": "Operator", "x": 0, "y": 0, "width": 1440, "height": 900, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1},
            {"name": "Stage", "x": 1440, "y": 0, "width": 1280, "height": 720, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1}
        ]})");
        config.close();
        const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
        qputenv("QT_QPA_PLATFORM", "offscreen:configfile=" + config.fileName().toLocal8Bit());

        RenderLink::Options options;
        options.serverName = QStringLiteral("lumen-stage-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        options.executable = QStringLiteral(LUMEN_RENDER_PATH);
        options.arguments = {QStringLiteral("--stage"), QStringLiteral("--screen=1"), QStringLiteral("--test-mode"),
                             QStringLiteral("--watch-pid=%1").arg(QCoreApplication::applicationPid())};
        RenderLink link(options);
        auto started = link.start();
        qputenv("QT_QPA_PLATFORM", platform);
        QVERIFY2(started.ok(), started.ok() ? "" : qPrintable(started.error().message));

        auto status = link.request({{"cmd", "status"}});
        QVERIFY(status.ok());
        QCOMPARE(status.value().value("kind").toString(), QStringLiteral("stage"));
        QCOMPARE(status.value().value("screen").toString(), QStringLiteral("Stage"));

        // The audience protocol is refused; the stage state is taken.
        QVERIFY(link.request({{"cmd", "clock"}, {"mode", "manual"}}).ok());
        const qint64 wall = quarterPastTen();
        QVERIFY(link.request({{"cmd", "clock"}, {"wallMs", wall}}).ok());
        QVERIFY(link.request({{"cmd", "stage"}, {"stage", sampleState(wall, "default")}}).ok());

        QVariantMap texts;
        QDeadlineTimer deadline(5000);
        while (!deadline.hasExpired()) {
            auto reply = link.request({{"cmd", "texts"}, {"prefix", "stage"}});
            QVERIFY(reply.ok());
            texts = reply.value().value("texts").toObject().toVariantMap();
            if (texts.contains("stageNow"))
                break;
            QTest::qWait(50);
        }
        QVERIFY(texts.value("stageNow").toString().startsWith(QStringLiteral("'Twas grace")));
        QVERIFY(texts.value("stageNext").toString().startsWith(QStringLiteral("How precious")));
        QCOMPARE(texts.value("stageClock").toString(), QStringLiteral("10:15"));
        QCOMPARE(texts.value("stageMessage").toString(), QStringLiteral("Repeat the last verse"));
        (void)link.quit();
    }
};

QTEST_MAIN(TestStage)
#include "tst_stage.moc"
