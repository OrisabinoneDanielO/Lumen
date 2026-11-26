#include "support/timing.h"
#include "operatorharness.h"
#include "outputwindow.h"

#include "lumenfile.h"
#include "planstore.h"

#include "lumen/core/data/repository.h"

#include <QFile>
#include <QJsonDocument>
#include <QQuickItem>

#include <functional>
#include <QtEndian>

#include <cmath>

using namespace lumen;
using namespace lumen::app;
using namespace lumen::core::data;
using namespace lumen::core::show;

// Batch 20 "Done when": slides follow a backing track to the frame.
namespace {

// A tone of `seconds` as a 48 kHz, 16-bit mono WAV file.
bool writeTone(const QString &path, int seconds)
{
    const int rate = 48000;
    const int samples = rate * seconds;
    QByteArray data(samples * 2, Qt::Uninitialized);
    for (int i = 0; i < samples; ++i)
        qToLittleEndian<qint16>(qint16(3000 * std::sin(2 * M_PI * 440.0 * i / rate)), data.data() + 2 * i);
    QByteArray header("RIFF");
    const auto u32 = [&](quint32 v) { char b[4]; qToLittleEndian(v, b); header.append(b, 4); };
    const auto u16 = [&](quint16 v) { char b[2]; qToLittleEndian(v, b); header.append(b, 2); };
    u32(36 + data.size());
    header += "WAVEfmt ";
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    header += "data";
    u32(data.size());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(header + data) == header.size() + data.size();
}

QJsonObject timeline(const QString &audio, const QList<std::pair<int, QString>> &cues, bool playing = true, int offsetMs = 0,
                     const QString &id = QStringLiteral("run-1"))
{
    QJsonArray list;
    for (const auto &[ms, text] : cues)
        list.append(QJsonObject{{"ms", ms}, {"content", contentToJson(Content::textContent(text))}});
    return {{"id", id}, {"audio", audio}, {"playing", playing}, {"offsetMs", offsetMs}, {"cues", list}};
}

// The slide text on screen (the Slide layer's incoming content).
QString onScreen(render::OutputWindow &output)
{
    QString text;
    std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
        if (!item || !item->isVisible())
            return;
        if (item->metaObject()->className() == QByteArray("QQuickText") && !item->property("text").toString().isEmpty()
            && item->opacity() > 0)
            text = item->property("text").toString();
        for (QQuickItem *child : item->childItems())
            find(child);
    };
    find(output.window()->contentItem());
    return text;
}

} // namespace

class TestTimecode : public QObject
{
    Q_OBJECT

private slots:
    // On a frame-by-frame clock: each cue appears on the first frame at or
    // after its time, and the frame before shows the one before.
    void cuesLandOnTheFrame()
    {
        render::OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(480, 270));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        output.setManualClock(true);
        Scene scene;
        scene.sequence = 1;
        const QList<std::pair<int, QString>> cues{{0, "One"}, {1000, "Two"}, {2517, "Three"}, {4000, "Four"}};
        scene.layer(Layer::Slide).content = Content::cued(timeline(QString(), cues));
        output.setScene(scene);
        QCOMPARE(onScreen(output), QStringLiteral("One"));

        // Step through 5 seconds of 60 fps frames.
        const double frame = 1000.0 / 60.0;
        QHash<QString, qint64> firstSeen{{"One", 0}};
        qint64 now = 0;
        QString previous = QStringLiteral("One");
        for (int f = 1; f <= 300; ++f) {
            const qint64 next = qint64(std::llround(f * frame));
            output.advanceClock(next - now);
            now = next;
            const QString text = onScreen(output);
            if (text != previous) {
                firstSeen.insert(text, now);
                previous = text;
            }
        }
        for (const auto &[ms, text] : cues) {
            QVERIFY2(firstSeen.contains(text), qPrintable(text));
            // The first frame at or after the cue's time: never early, at
            // most one frame late.
            QVERIFY2(firstSeen.value(text) >= ms && firstSeen.value(text) < ms + frame,
                     qPrintable(QStringLiteral("%1 at %2 ms, cue %3 ms").arg(text).arg(firstSeen.value(text)).arg(ms)));
        }
        const QJsonObject status = output.timelineStatus();
        QCOMPARE(status.value("audio").toString(), QStringLiteral("none"));
        QCOMPARE(status.value("cue").toInt(), 3);
        for (const QJsonValue &s : status.value("switches").toArray()) {
            const qint64 late = s.toObject().value("atMs").toInteger() - s.toObject().value("cueMs").toInteger();
            QVERIFY(late >= 0 && late < frame);
        }

        // Paused at 1.5 s: Two stays, whatever the clock does.
        scene.sequence = 2;
        scene.layer(Layer::Slide).content = Content::cued(timeline(QString(), cues, false, 1500, "run-2"));
        output.setScene(scene);
        output.advanceClock(5000);
        QCOMPARE(onScreen(output), QStringLiteral("Two"));
        // Played from 2.5 s: Three 17 ms later.
        scene.sequence = 3;
        scene.layer(Layer::Slide).content = Content::cued(timeline(QString(), cues, true, 2500, "run-3"));
        output.setScene(scene);
        QCOMPARE(onScreen(output), QStringLiteral("Two"));
        output.advanceClock(16);
        QCOMPARE(onScreen(output), QStringLiteral("Two"));
        output.advanceClock(1);
        QCOMPARE(onScreen(output), QStringLiteral("Three"));
    }

    // With a real track: the render clock keeps to the audio, and every cue
    // comes up within a frame of its time in the track.
    void cuesFollowTheAudio()
    {
        LUMEN_TIMING_CHECK();
        QTemporaryDir dir;
        const QString track = dir.filePath(QStringLiteral("backing.wav"));
        QVERIFY(writeTone(track, 6));
        render::OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(480, 270));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        Scene scene;
        scene.sequence = 1;
        scene.layer(Layer::Slide).content =
            Content::cued(timeline(track, {{0, "Intro"}, {1000, "Verse"}, {2000, "Chorus"}, {3250, "Bridge"}, {4500, "End"}}));
        output.setScene(scene);
        QJsonObject status;
        QTRY_VERIFY_WITH_TIMEOUT((status = output.timelineStatus(), status.value("audio").toString() != QLatin1String("waiting")), 4000);
        if (status.value("audio").toString() == QLatin1String("unavailable"))
            QSKIP("no audio output here");
        QCOMPARE(status.value("audio").toString(), QStringLiteral("playing"));
        QTRY_VERIFY_WITH_TIMEOUT((status = output.timelineStatus(), status.value("cue").toInt() == 4), 8000);
        qInfo("switches %s timing %s", QJsonDocument(status.value("switches").toArray()).toJson(QJsonDocument::Compact).constData(),
              qPrintable(QStringLiteral("tick max gap %1 ms").arg(output.timing().tickMaxGapMs)));
        // Each cue on the first frame at or after its time (a frame here is
        // the output's tick, about 17 ms, longer when the machine is busy),
        // and slides only go forward.
        const double frame = std::max(17.0, output.timing().tickMaxGapMs);
        int last = -1;
        for (const QJsonValue &s : status.value("switches").toArray()) {
            const qint64 late = s.toObject().value("atMs").toInteger() - s.toObject().value("cueMs").toInteger();
            QVERIFY2(late >= 0 && late <= frame, qPrintable(QStringLiteral("cue %1 came %2 ms after its time")
                                                                .arg(s.toObject().value("cue").toInt()).arg(late)));
            QVERIFY(s.toObject().value("cue").toInt() > last);
            last = s.toObject().value("cue").toInt();
        }
        // The clock keeps to the audio: within the player's reporting step
        // (where the sound is somewhere in) and a frame.
        const qint64 step = status.value("audioStepMs").toInteger();
        const qint64 worst = status.value("worstSyncMs").toInteger();
        qInfo("timeline vs audio: worst %lld ms (audio reports every %lld ms)", worst, step);
        QVERIFY(worst <= step / 2 + 17);
        QTRY_COMPARE(onScreen(output), QStringLiteral("End"));
    }

    // Recording, nudging and playing cues from the operator window.
    void recordAndPlay()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        ui.controller->selectItem(2); // Amazing Grace
        CueController &cues = *ui.cues;
        QVERIFY(cues.available());
        QCOMPARE(cues.itemTitle(), QStringLiteral("Amazing Grace"));
        QTemporaryDir dir;
        const QString track = dir.filePath(QStringLiteral("grace.wav"));
        QVERIFY(writeTone(track, 2));
        QVERIFY(cues.setTrack(QUrl::fromLocalFile(track)));
        QCOMPARE(cues.trackName(), QStringLiteral("grace.wav"));

        qint64 t = 0;
        cues.setRecordingClock([&t] { return t; });
        QVERIFY(cues.startRecording());
        QVERIFY(ui.controller->goLive(0));
        t = 4200;
        QVERIFY(ui.controller->next());
        t = 9100;
        QVERIFY(ui.controller->next());
        cues.stopRecording();
        auto *rows = qobject_cast<RowModel *>(cues.cues());
        QCOMPARE(rows->rowCount(), 3);
        QCOMPARE(rows->get(1).value("ms").toLongLong(), 4200);
        QCOMPARE(rows->get(1).value("slide").toInt(), 1);
        QCOMPARE(rows->get(2).value("time").toString(), QStringLiteral("0:09.10"));
        cues.nudge(1, -2);
        QCOMPARE(rows->get(1).value("ms").toLongLong(), 4200 - 2 * CueController::frameMs);

        // Kept with the song.
        const auto stored = Repository<Presentation>(ui.database()).get(ui.controller->selectedPresentationId());
        const QJsonObject kept = stored.value().metadata.value("timeline").toObject();
        QCOMPARE(kept.value("cues").toArray().size(), 3);
        QVERIFY(!kept.value("audioAssetId").toString().isEmpty());

        // Played on the output: one cued content with the slides' text.
        QVERIFY(cues.play());
        const Content live = ui.show.scene().layer(Layer::Slide).content;
        QCOMPARE(live.kind, Content::Kind::Cued);
        QCOMPARE(live.timeline.value("audio").toString(), QFileInfo(track).absoluteFilePath());
        const QJsonArray sent = live.timeline.value("cues").toArray();
        QCOMPARE(sent.size(), 3);
        auto *slides = qobject_cast<RowModel *>(ui.controller->slides());
        QCOMPARE(contentFromJson(sent[1].toObject().value("content"))->text, slides->get(1).value("text").toString());
        QVERIFY(cues.playing());
        cues.stop();
        QVERIFY(!cues.playing());

        // The Cues popover from the slide grid.
        QVERIFY(ui.click("cuesButton"));
        ui.settle();
        QVERIFY(ui.item("cuesPopover") && ui.item("cue-2"));
        QVERIFY(ui.click("cueLater-0"));
        QCOMPARE(rows->get(0).value("ms").toLongLong(), CueController::frameMs);
    }

    // A .lumen file carries the cues and the backing track.
    void lumenFileCarriesTheTrack()
    {
        auto source = Database::open(Database::inMemory());
        QVERIFY(source.ok());
        QVERIFY(seedSampleService(*source.value()).ok());
        QTemporaryDir dir;
        const QString track = dir.filePath(QStringLiteral("track.wav"));
        QVERIFY(writeTone(track, 1));
        Database &db = *source.value();
        QString songId;
        const auto all = Repository<Presentation>(db).list();
        for (const Presentation &p : all.value())
            songId = p.title == QLatin1String("Amazing Grace") ? p.id : songId;
        Presentation song = Repository<Presentation>(db).get(songId).value();
        auto asset = Repository<MediaAsset>(db).create({.libraryId = song.libraryId, .filePath = track,
                                                        .hash = MediaLibrary::fingerprint(track)});
        QVERIFY(asset.ok());
        song.metadata.insert("timeline", QJsonObject{{"audioAssetId", asset.value().id},
                                                     {"cues", QJsonArray{QJsonObject{{"ms", 0}, {"slide", 0}},
                                                                         QJsonObject{{"ms", 2500}, {"slide", 1}}}}});
        QVERIFY(Repository<Presentation>(db).update(song).ok());
        auto plan = plans::create(db, QStringLiteral("With a track"), QDate(2026, 12, 13));
        QVERIFY(Repository<PlanItem>(db).create({.planId = plan.value().id, .presentationId = songId}).ok());
        const QString file = dir.filePath(QStringLiteral("track.lumen"));
        QVERIFY(lumenfile::exportPlan(db, plan.value().id, file).ok());

        auto target = Database::open(Database::inMemory());
        auto imported = lumenfile::importFile(*target.value(), file, dir.filePath(QStringLiteral("imported")));
        QVERIFY2(imported.ok(), imported.ok() ? "" : qPrintable(imported.error().message));
        QString copyId;
        const auto copies = Repository<Presentation>(*target.value()).list();
        for (const Presentation &p : copies.value())
            copyId = p.title == QLatin1String("Amazing Grace") ? p.id : copyId;
        const QJsonObject copied = Repository<Presentation>(*target.value()).get(copyId).value().metadata.value("timeline").toObject();
        QCOMPARE(copied.value("cues").toArray().size(), 2);
        const auto copiedAsset = Repository<MediaAsset>(*target.value()).get(copied.value("audioAssetId").toString());
        QVERIFY(copiedAsset.ok());
        QVERIFY(copiedAsset.value().filePath.startsWith(dir.filePath(QStringLiteral("imported"))));
        QCOMPARE(MediaLibrary::fingerprint(copiedAsset.value().filePath), MediaLibrary::fingerprint(track));
    }
};

QTEST_MAIN(TestTimecode)
#include "tst_timecode.moc"
