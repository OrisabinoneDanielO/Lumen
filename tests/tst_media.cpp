#include "medialibrary.h"
#include "operatorharness.h"

#include "lumen/core/data/repository.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace lumen;
using namespace lumen::core;
using namespace lumen::core::data;
using namespace lumen::core::show;
using lumen::test::OperatorHarness;

// Media (batch 9): importing videos and pictures, showing them as slides,
// motion backgrounds, preloading the next video, and the Media tab.
class TestMedia : public QObject
{
    Q_OBJECT

private:
    static QString media(const char *name) { return QStringLiteral(LUMEN_MEDIA_DIR "/") + QLatin1String(name); }

    static std::unique_ptr<Database> emptyLibrary()
    {
        return std::move(Database::open(Database::inMemory())).value();
    }

    // Finds a media row by title in the controller's media model.
    static int mediaRow(app::OperatorController &controller, const QString &title)
    {
        auto *model = qobject_cast<app::RowModel *>(controller.media());
        for (int row = 0; row < model->count(); ++row) {
            if (model->get(row).value(QStringLiteral("title")).toString() == title)
                return row;
        }
        return -1;
    }

    // The Layers panel's text for one layer.
    static QString layerValue(app::OperatorController &controller, const QString &name)
    {
        for (const QVariant &row : controller.layers()) {
            if (row.toMap().value(QStringLiteral("name")).toString() == name)
                return row.toMap().value(QStringLiteral("value")).toString();
        }
        return {};
    }

private slots:
    void importReadsLengthAndMakesThumbnails()
    {
        auto db = emptyLibrary();
        QTemporaryDir thumbnails;
        app::MediaLibrary library(*db, thumbnails.path());
        const auto report = library.importFiles({media("loop-1080p-vp9.webm"), media("clip-360p-h264.mp4"),
                                                 media("bars-720p.png")});
        QVERIFY2(report.imported == 3, qPrintable(report.messages.join(u'\n')));
        QCOMPARE(report.summary(), QStringLiteral("3 imported"));

        const auto rows = library.list();
        QVERIFY(rows.ok());
        QCOMPARE(rows.value().size(), std::size_t(3));
        for (const app::MediaRow &row : rows.value()) {
            QVERIFY2(QFile::exists(row.thumbnail), qPrintable(row.title));
            const QImage thumb(row.thumbnail);
            QVERIFY(thumb.width() <= 320 && thumb.height() <= 180);
            QVERIFY(!thumb.isNull());
            if (row.title == QLatin1String("loop-1080p-vp9")) {
                QCOMPARE(row.kind, QStringLiteral("video"));
                QVERIFY2(std::abs(row.durationMs - 10000) < 200, qPrintable(QString::number(row.durationMs)));
                QVERIFY(row.loop); // short: a motion loop
                QCOMPARE(thumb.width(), 320);
            } else if (row.title == QLatin1String("clip-360p-h264")) {
                QCOMPARE(row.kind, QStringLiteral("video"));
                QVERIFY(std::abs(row.durationMs - 3000) < 200);
            } else {
                QCOMPARE(row.title, QStringLiteral("bars-720p"));
                QCOMPARE(row.kind, QStringLiteral("image"));
                QCOMPARE(row.durationMs, qint64(0));
                QVERIFY(!row.loop);
            }
        }
        // Each is a media presentation with one slide, ready for a plan.
        const auto presentations = Repository<Presentation>(*db).list();
        QCOMPARE(presentations.value().size(), std::size_t(3));
        for (const Presentation &p : presentations.value())
            QCOMPARE(p.kind, PresentationKind::Media);
    }

    void duplicatesAndBadFilesAreReported()
    {
        auto db = emptyLibrary();
        QTemporaryDir folder;
        app::MediaLibrary library(*db, folder.filePath(QStringLiteral("thumbs")));
        QCOMPARE(library.importFiles({media("bars-720p.png")}).imported, 1);

        // The same picture under another name is still the same file.
        const QString copy = folder.filePath(QStringLiteral("copy.png"));
        QVERIFY(QFile::copy(media("bars-720p.png"), copy));
        QFile junk(folder.filePath(QStringLiteral("broken.mp4")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray(4096, 'x'));
        junk.close();
        QFile text(folder.filePath(QStringLiteral("notes.txt")));
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("hello");
        text.close();

        const auto report = library.importFiles({copy, junk.fileName(), text.fileName(),
                                                 folder.filePath(QStringLiteral("missing.png"))});
        QCOMPARE(report.imported, 0);
        QCOMPARE(report.skipped, 1);
        QCOMPARE(report.failed, 3);
        QCOMPARE(report.summary(), QStringLiteral("0 imported, 1 skipped, 3 failed"));
        QVERIFY(report.messages.join(u'\n').contains(QStringLiteral("copy.png: already in the library")));
        QVERIFY(report.messages.join(u'\n').contains(QStringLiteral("broken.mp4: the video could not be read")));
        QCOMPARE(library.list().value().size(), std::size_t(1));
    }

    void videoGoesLiveOnTheMediaLayer()
    {
        OperatorHarness h;
        QString error;
        QVERIFY2(h.load(false, &error), qPrintable(error));
        QVERIFY(h.controller->mediaAvailable());
        QCOMPARE(h.controller->importMedia({QUrl::fromLocalFile(media("clip-360p-h264.mp4")),
                                            QUrl::fromLocalFile(media("bars-720p.png"))}),
                 QStringLiteral("2 imported"));

        QSignalSpy preload(h.controller.get(), &app::OperatorController::previewVideoChanged);
        const int clip = mediaRow(*h.controller, QStringLiteral("clip-360p-h264"));
        QVERIFY(h.controller->showMedia(clip));
        QCOMPARE(h.controller->selectedMedia(), clip);
        // In Preview, so the renderer is asked to open it ahead of time.
        QCOMPARE(preload.count(), 1);
        QCOMPARE(preload.at(0).at(0).toString(), media("clip-360p-h264.mp4"));
        QCOMPARE(h.controller->previewVideo(), media("clip-360p-h264.mp4"));
        QVERIFY(h.controller->previewPoster().isLocalFile());

        // A song is live first; the video replaces its text.
        h.controller->goLive(0);
        QCOMPARE(h.show.scene().layer(Layer::Media).content.kind, Content::Kind::Video);
        const Content video = h.show.scene().layer(Layer::Media).content;
        QCOMPARE(video.videoPath, media("clip-360p-h264.mp4"));
        QVERIFY(!video.loop);
        QCOMPARE(video.volume, 1.0);
        QVERIFY(h.show.scene().layer(Layer::Slide).content.isEmpty());
        QVERIFY(h.controller->livePoster().isLocalFile());

        // Clear Slide also stops full-screen media.
        h.controller->clearLayer(QStringLiteral("slide"));
        QVERIFY(h.show.scene().layer(Layer::Media).content.isEmpty());

        // A picture goes live the same way.
        QVERIFY(h.controller->showMedia(mediaRow(*h.controller, QStringLiteral("bars-720p"))));
        h.controller->goLive(0);
        QCOMPARE(h.show.scene().layer(Layer::Media).content.kind, Content::Kind::Image);
        QCOMPARE(h.show.scene().layer(Layer::Media).content.imagePath, media("bars-720p.png"));

        // The next text slide takes the media off.
        QVERIFY(h.controller->addMediaToPlan(clip));
        h.controller->selectItem(0);
        h.controller->goLive(0);
        QVERIFY(h.show.scene().layer(Layer::Media).content.isEmpty());
        QVERIFY(!h.show.scene().layer(Layer::Slide).content.isEmpty());
    }

    void motionBackgroundStaysUnderThemedSlides()
    {
        OperatorHarness h;
        QString error;
        QVERIFY2(h.load(false, &error), qPrintable(error));
        h.controller->importMedia({QUrl::fromLocalFile(media("loop-1080p-vp9.webm"))});
        QVERIFY(h.controller->useAsBackground(0));
        const Content background = h.show.scene().layer(Layer::Background).content;
        QCOMPARE(background.kind, Content::Kind::Video);
        QVERIFY(background.loop);
        QCOMPARE(background.volume, 0.0); // motion loops are silent

        // Songs carry a theme background, but the operator's choice stays.
        h.controller->selectItem(0);
        h.controller->goLive(0);
        h.controller->goLive(1);
        QCOMPARE(h.show.scene().layer(Layer::Background).content, background);
        QVERIFY(!h.show.scene().layer(Layer::Slide).content.isEmpty());

        // After Clear Background the theme's background returns.
        h.controller->clearLayer(QStringLiteral("background"));
        h.controller->goLive(0);
        QVERIFY(h.show.scene().layer(Layer::Background).content.kind != Content::Kind::Video);
    }

    // Each video plays once and stops, or loops until cleared: the operator
    // chooses above the slides; it is kept with the video and a video that
    // is playing takes it at once.
    void videoPlaysOnceOrLoops()
    {
        test::OperatorHarness h;
        QString error;
        QVERIFY2(h.load(false, &error), qPrintable(error));
        h.controller->importMedia({QUrl::fromLocalFile(media("clip-360p-h264.mp4"))});
        const int clip = mediaRow(*h.controller, QStringLiteral("clip-360p-h264"));
        QVERIFY(h.controller->showMedia(clip));
        QVERIFY(h.controller->shownIsVideo());
        QVERIFY(!h.controller->shownVideoLoops()); // Play once by default
        h.settle();
        QVERIFY(h.item(QStringLiteral("videoOnce"))->isVisible());
        QVERIFY(h.controller->playMedia(clip));
        QCOMPARE(h.show.scene().layer(Layer::Media).content.kind, Content::Kind::Video);
        QVERIFY(!h.show.scene().layer(Layer::Media).content.loop);

        // While it plays: Loop applies at once, and is kept with the video.
        QVERIFY(h.click(QStringLiteral("videoLoop")));
        QVERIFY(h.controller->shownVideoLoops());
        QVERIFY(h.show.scene().layer(Layer::Media).content.loop);
        auto *cards = qobject_cast<app::RowModel *>(h.controller->media());
        const auto kept = Repository<Presentation>(h.database()).get(cards->get(clip).value("presentationId").toString());
        QVERIFY(kept.ok() && kept.value().metadata.value("videoLoop").toBool());
        QVERIFY(cards->get(clip).value("loop").toBool()); // the card says Loop
        // Play once again: from now on it stops at its end.
        QVERIFY(h.click(QStringLiteral("videoOnce")));
        QVERIFY(!h.show.scene().layer(Layer::Media).content.loop);
        QVERIFY(h.controller->playMedia(clip));
        QVERIFY(!h.show.scene().layer(Layer::Media).content.loop);

        // Not a video: no choice shown.
        h.controller->selectItem(2); // a song
        h.settle();
        QVERIFY(!h.controller->shownIsVideo());
        QVERIFY(!h.item(QStringLiteral("videoOnce"))->isVisible());
    }

    void timesReadAndWritten()
    {
        using app::OperatorController;
        QCOMPARE(OperatorController::parseTime(QStringLiteral("1:05")), qint64(65000));
        QCOMPARE(OperatorController::parseTime(QStringLiteral("65")), qint64(65000));
        QCOMPARE(OperatorController::parseTime(QStringLiteral(" 0:02.5 ")), qint64(2500));
        QCOMPARE(OperatorController::parseTime(QString()), qint64(0));
        QCOMPARE(OperatorController::parseTime(QStringLiteral("1:75")), qint64(-1));
        QCOMPARE(OperatorController::parseTime(QStringLiteral("soon")), qint64(-1));
        QCOMPARE(OperatorController::formatTime(65000), QStringLiteral("1:05"));
        QCOMPARE(OperatorController::formatTime(0), QStringLiteral("0:00"));
    }

    // Start and end points are kept with the video and go live with it;
    // the Live column pauses, plays and jumps along the video on screen.
    void startEndAndTransport()
    {
        test::OperatorHarness h;
        QString error;
        QVERIFY2(h.load(false, &error), qPrintable(error));
        h.controller->importMedia({QUrl::fromLocalFile(media("loop-1080p-vp9.webm"))});
        const int clip = mediaRow(*h.controller, QStringLiteral("loop-1080p-vp9"));
        QVERIFY(h.controller->showMedia(clip));
        h.settle();
        QVERIFY(h.item(QStringLiteral("videoStart"))->isVisible());
        QCOMPARE(h.controller->shownVideoStart(), QStringLiteral("0:00"));
        QCOMPARE(h.controller->shownVideoEnd(), QString());

        // Typed in the boxes above the slides.
        QVERIFY(h.click(QStringLiteral("videoStart")));
        QTest::keyClick(h.window, Qt::Key_A, Qt::ControlModifier);
        for (const char c : QByteArrayLiteral("0:02"))
            QTest::keyClick(h.window, c);
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_COMPARE(h.controller->shownVideoStart(), QStringLiteral("0:02"));
        QVERIFY(h.click(QStringLiteral("videoEnd")));
        for (const char c : QByteArrayLiteral("8"))
            QTest::keyClick(h.window, c);
        QTest::keyClick(h.window, Qt::Key_Return);
        QTRY_COMPARE(h.controller->shownVideoEnd(), QStringLiteral("0:08"));
        auto *cards = qobject_cast<app::RowModel *>(h.controller->media());
        const auto kept = Repository<Presentation>(h.database()).get(cards->get(clip).value("presentationId").toString());
        QVERIFY(kept.ok());
        QCOMPARE(kept.value().metadata.value("trimInMs").toInteger(), 2000);
        QCOMPARE(kept.value().metadata.value("trimOutMs").toInteger(), 8000);
        // An end before the start is refused, with a reason.
        QVERIFY(!h.controller->setShownVideoTrim(QStringLiteral("0:05"), QStringLiteral("0:03")));
        QCOMPARE(h.controller->shownVideoEnd(), QStringLiteral("0:08"));

        QVERIFY(h.controller->playMedia(clip));
        const Content live = h.show.scene().layer(Layer::Media).content;
        QCOMPARE(live.trimInMs, 2000);
        QCOMPARE(live.trimOutMs, 8000);

        // The output reports where it is: the Live column shows it.
        QVERIFY(!h.controller->liveVideo());
        h.controller->setOutputStatus({{"videoPosition", QJsonObject{{"path", live.videoPath}, {"positionMs", 3000},
                                                                     {"durationMs", 8000}, {"playing", true}}}});
        QVERIFY(h.controller->liveVideo());
        QCOMPARE(h.controller->liveVideoPositionMs(), 3000.0);
        h.settle();
        QTRY_VERIFY(h.item(QStringLiteral("videoTransport"))->isVisible());
        // LUMEN_SCREENSHOT_DIR: the Live column with the video's controls.
        if (const QString dir = qEnvironmentVariable("LUMEN_SCREENSHOT_DIR"); !dir.isEmpty())
            QVERIFY(h.window->grabWindow().save(dir + QStringLiteral("/video-transport.png")));
        QSignalSpy control(h.controller.get(), &app::OperatorController::videoControlRequested);
        QVERIFY(h.click(QStringLiteral("videoPlayPause")));
        QCOMPARE(control.count(), 1);
        QCOMPARE(control.at(0).at(0).toString(), QStringLiteral("pause"));
        QVERIFY(!h.controller->liveVideoPlaying());
        // A click on the bar three quarters along jumps there.
        QQuickItem *bar = h.item(QStringLiteral("videoSeekBar"));
        QTest::mouseClick(h.window, Qt::LeftButton, {}, bar->mapToScene(QPointF(bar->width() * 0.75, bar->height() / 2)).toPoint());
        QCOMPARE(control.count(), 2);
        QCOMPARE(control.at(1).at(0).toString(), QStringLiteral("seek"));
        QVERIFY(qAbs(control.at(1).at(1).toLongLong() - 6000) < 200);

        // Cleared: no transport.
        h.controller->clearLayer(QStringLiteral("slide"));
        QVERIFY(!h.controller->liveVideo());
        h.settle();
        QTRY_VERIFY(!h.item(QStringLiteral("videoTransport"))->isVisible());
    }

    // A picture or a video as the Logo (F6), kept for next time.
    void pictureOrVideoAsLogo()
    {
        test::OperatorHarness h;
        QString error;
        QVERIFY2(h.load(false, &error), qPrintable(error));
        h.controller->importMedia({QUrl::fromLocalFile(media("bars-720p.png")), QUrl::fromLocalFile(media("loop-1080p-vp9.webm"))});
        QSignalSpy kept(h.controller.get(), &app::OperatorController::logoMediaChanged);
        QVERIFY(h.controller->useAsLogo(mediaRow(*h.controller, QStringLiteral("bars-720p"))));
        QCOMPARE(h.show.scene().logoContent.kind, Content::Kind::Image);
        QCOMPARE(h.show.scene().logoContent.imagePath, media("bars-720p.png"));
        QCOMPARE(kept.count(), 1);
        QCOMPARE(kept.at(0).at(0).toString(), media("bars-720p.png"));
        QVERIFY(h.controller->useAsLogo(mediaRow(*h.controller, QStringLiteral("loop-1080p-vp9"))));
        const Content logo = h.show.scene().logoContent;
        QCOMPARE(logo.kind, Content::Kind::Video);
        QVERIFY(logo.loop);
        QCOMPARE(logo.volume, 0.0); // silent
        h.settle();
        QVERIFY(h.item(QStringLiteral("resetLogo")) != nullptr);
        h.controller->resetLogo();
        QCOMPARE(h.show.scene().logoContent, core::show::Scene{}.logoContent);
        QVERIFY(kept.last().at(0).toString().isEmpty());
        // A kept logo whose file has gone: Lumen's own.
        h.controller->setLogoMedia(QStringLiteral("/no/such/logo.png"));
        QCOMPARE(h.show.scene().logoContent, core::show::Scene{}.logoContent);
    }

    // Media taken out of the library: off the screen and out of its plan;
    // the file stays on disk, the thumbnail goes. Remove asks first.
    void removingMedia()
    {
        test::OperatorHarness h;
        QString error;
        QVERIFY2(h.load(false, &error), qPrintable(error));
        h.controller->importMedia({QUrl::fromLocalFile(media("clip-360p-h264.mp4")), QUrl::fromLocalFile(media("bars-720p.png"))});
        const int clip = mediaRow(*h.controller, QStringLiteral("clip-360p-h264"));
        const int before = h.controller->media()->property("count").toInt();
        const int planBefore = h.controller->plan()->property("count").toInt();
        QVERIFY(h.controller->addMediaToPlan(clip));
        QCOMPARE(h.controller->plan()->property("count").toInt(), planBefore + 1);
        QVERIFY(h.controller->playMedia(clip));
        QCOMPARE(h.show.scene().layer(Layer::Media).content.kind, Content::Kind::Video);
        auto *cards = qobject_cast<app::RowModel *>(h.controller->media());
        const QString thumbnail = cards->get(clip).value("thumbnail").toUrl().toLocalFile();
        QVERIFY(QFileInfo::exists(thumbnail));

        QVERIFY(h.controller->removeMedia(clip));
        QCOMPARE(h.controller->media()->property("count").toInt(), before - 1);
        QCOMPARE(h.controller->plan()->property("count").toInt(), planBefore);
        QVERIFY(h.show.scene().layer(Layer::Media).content.isEmpty());
        QVERIFY(QFileInfo::exists(media("clip-360p-h264.mp4"))); // the file is untouched
        QVERIFY(!QFileInfo::exists(thumbnail));
        QVERIFY(h.controller->mediaSummary().contains(QStringLiteral("plan item")));
        QCOMPARE(mediaRow(*h.controller, QStringLiteral("clip-360p-h264")), -1);

        // In the window: Remove, then Sure?
        const int picture = mediaRow(*h.controller, QStringLiteral("bars-720p"));
        QVERIFY(h.controller->showMedia(picture));
        h.settle();
        QQuickItem *library = h.item(QStringLiteral("library"));
        library->setProperty("page", 2);
        h.settle();
        QVERIFY(h.click(QStringLiteral("removeMedia")));
        QCOMPARE(h.controller->media()->property("count").toInt(), before - 1); // asked, not gone
        QVERIFY(h.click(QStringLiteral("removeMedia")));
        QTRY_COMPARE(h.controller->media()->property("count").toInt(), before - 2);
    }

    // Audio items (EasyWorship's audio): music beside the slides. Live, it
    // plays without touching what is on screen; Play once or Loop; Stop in
    // the Layers list, or Clear All.
    void audioItems()
    {
        test::OperatorHarness h;
        QString error;
        QVERIFY2(h.load(false, &error), qPrintable(error));
        h.controller->importMedia({QUrl::fromLocalFile(media("tone-2s.wav"))});
        const int tone = mediaRow(*h.controller, QStringLiteral("tone-2s"));
        QVERIFY2(tone >= 0, qPrintable(h.controller->mediaSummary()));
        auto *cards = qobject_cast<app::RowModel *>(h.controller->media());
        QCOMPARE(cards->get(tone).value("kind").toString(), QStringLiteral("audio"));
        QCOMPARE(cards->get(tone).value("duration").toString(), QStringLiteral("0:02"));

        // A song is up; the music starts beside it.
        h.controller->selectItem(2);
        QVERIFY(h.controller->goLive(0));
        const Content slide = h.show.scene().layer(Layer::Slide).content;
        QVERIFY(h.controller->showMedia(tone));
        QVERIFY(h.controller->shownIsAudio());
        QVERIFY(h.controller->shownIsVideo()); // Play once / Loop
        QVERIFY(h.controller->playMedia(tone));
        QCOMPARE(h.show.scene().audio.videoPath, media("tone-2s.wav"));
        QCOMPARE(h.show.scene().layer(Layer::Slide).content, slide); // untouched
        QVERIFY(h.show.scene().layer(Layer::Media).content.isEmpty());
        QCOMPARE(h.controller->liveAudio(), QStringLiteral("tone-2s"));
        // Loop chosen while it plays: at once.
        h.controller->setShownVideoLoops(true);
        QVERIFY(h.show.scene().audio.loop);
        // Next still steps through the song.
        h.controller->selectItem(2);
        QVERIFY(h.controller->next());
        QCOMPARE(h.show.scene().audio.videoPath, media("tone-2s.wav"));
        // Stop, in the Layers list.
        h.settle();
        QTRY_VERIFY(h.item(QStringLiteral("audioStop")) && h.item(QStringLiteral("audioStop"))->isVisible());
        // (Below the fold of the Live column at this window size: pressed
        // directly rather than clicked at a point out of view.)
        QVERIFY(QMetaObject::invokeMethod(h.item(QStringLiteral("audioStop")), "clicked"));
        QVERIFY(h.show.scene().audio.isEmpty());
        // Clear All stops it too.
        QVERIFY(h.controller->playMedia(tone));
        h.controller->clearAll();
        QVERIFY(h.show.scene().audio.isEmpty());
    }

    // Edit mode never changes the screen: Use as background does nothing
    // there; in Show mode it puts the video under everything.
    void useAsBackgroundNotInEditMode()
    {
        test::OperatorHarness h;
        QString error;
        QVERIFY2(h.load(true, &error), qPrintable(error));
        h.controller->importMedia({QUrl::fromLocalFile(media("loop-1080p-vp9.webm"))});
        const int loop = mediaRow(*h.controller, QStringLiteral("loop-1080p-vp9"));
        QVERIFY(loop >= 0);
        h.controller->setProperty("mode", QStringLiteral("edit"));
        QCOMPARE(h.controller->property("mode").toString(), QStringLiteral("edit"));
        QVERIFY(!h.controller->useAsBackground(loop));
        QVERIFY(h.show.scene().layer(Layer::Background).content.isEmpty());
        h.controller->setProperty("mode", QStringLiteral("show"));
        QVERIFY(h.controller->useAsBackground(loop));
        QCOMPARE(h.show.scene().layer(Layer::Background).content.kind, Content::Kind::Video);
    }

    void mediaTabShowsCards()
    {
        OperatorHarness h;
        QString error;
        QVERIFY2(h.load(false, &error), qPrintable(error));
        h.controller->importMedia({QUrl::fromLocalFile(media("loop-1080p-vp9.webm")),
                                   QUrl::fromLocalFile(media("bars-720p.png"))});

        QVERIFY(h.item(QStringLiteral("songRow-0"))->isVisible());
        QVERIFY(h.click(QStringLiteral("libraryTab-2")));
        h.settle();
        QVERIFY(!h.item(QStringLiteral("importSongs"))->isVisible());
        QVERIFY(h.item(QStringLiteral("importMedia"))->isVisible());
        QQuickItem *card = h.item(QStringLiteral("mediaCard-0"));
        QVERIFY(card && card->isVisible());
        QVERIFY(h.item(QStringLiteral("mediaCard-1")));

        // Clicking a card puts it in Preview (and the slide grid, with its
        // thumbnail), not on the screen; double-clicking puts it on the
        // screen at once and a video plays. (The user's request: "choosing
        // a video or picture should take it to the preview window first but
        // double clicking on it should make it go live immediately".)
        const int loop = mediaRow(*h.controller, QStringLiteral("loop-1080p-vp9"));
        QVERIFY(h.click(QStringLiteral("mediaCard-%1").arg(loop)));
        QCOMPARE(h.controller->selectedMedia(), loop);
        QVERIFY(h.show.scene().layer(Layer::Media).content.isEmpty());
        h.settle();
        QQuickItem *thumb = h.item(QStringLiteral("slide-0"));
        QVERIFY(thumb);
        QVERIFY(thumb->property("thumbnail").toUrl().isLocalFile());
        QQuickItem *poster = h.item(QStringLiteral("previewFrame"));
        QVERIFY(poster->property("poster").toUrl().isLocalFile());
        QVERIFY(h.doubleClick(QStringLiteral("mediaCard-%1").arg(loop)));
        QCOMPARE(h.show.scene().layer(Layer::Media).content.kind, Content::Kind::Video);
        QVERIFY(h.show.scene().layer(Layer::Media).content.videoPath.endsWith(QLatin1String("loop-1080p-vp9.webm")));
        h.controller->clearLayer(QStringLiteral("media"));

        // Use as background puts it on the Background layer, looping.
        QVERIFY(h.click(QStringLiteral("useAsBackground")));
        QCOMPARE(h.show.scene().layer(Layer::Background).content.kind, Content::Kind::Video);
        QCOMPARE(layerValue(*h.controller, QStringLiteral("Background")), QStringLiteral("Motion loop: loop-1080p-vp9"));

        // Back to Songs.
        QVERIFY(h.click(QStringLiteral("libraryTab-0")));
        h.settle();
        QVERIFY(h.item(QStringLiteral("importSongs"))->isVisible());

        // In the service plan too: a click shows an item (Preview), a
        // double-click puts its first slide on the screen.
        QVERIFY(h.controller->addMediaToPlan(loop));
        h.controller->clearLayer(QStringLiteral("media"));
        h.settle();
        const int videoItem = h.controller->plan()->property("count").toInt() - 1;
        QVERIFY(h.click(QStringLiteral("planItem-%1").arg(videoItem)));
        QCOMPARE(h.controller->currentItem(), videoItem);
        QVERIFY(h.show.scene().layer(Layer::Media).content.isEmpty());
        QVERIFY(h.doubleClick(QStringLiteral("planItem-%1").arg(videoItem)));
        QCOMPARE(h.show.scene().layer(Layer::Media).content.kind, Content::Kind::Video);
        h.controller->clearLayer(QStringLiteral("media"));
        QVERIFY(h.click(QStringLiteral("planItem-2"))); // a song
        QCOMPARE(h.controller->currentItem(), 2);
        QVERIFY(h.show.scene().layer(Layer::Slide).content.isEmpty());
        QVERIFY(h.doubleClick(QStringLiteral("planItem-2")));
        QCOMPARE(h.controller->liveItem(), 2);
        QCOMPARE(h.show.scene().layer(Layer::Slide).content.kind, Content::Kind::Text);
    }
};

QTEST_MAIN(TestMedia)
#include "tst_media.moc"
