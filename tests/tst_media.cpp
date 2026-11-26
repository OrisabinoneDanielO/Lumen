#include "medialibrary.h"
#include "operatorharness.h"

#include "lumen/core/data/repository.h"

#include <QFile>
#include <QImage>
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

        // Clicking a card shows it in the slide grid with its thumbnail.
        const int loop = mediaRow(*h.controller, QStringLiteral("loop-1080p-vp9"));
        QVERIFY(h.click(QStringLiteral("mediaCard-%1").arg(loop)));
        QCOMPARE(h.controller->selectedMedia(), loop);
        h.settle();
        QQuickItem *thumb = h.item(QStringLiteral("slide-0"));
        QVERIFY(thumb);
        QVERIFY(thumb->property("thumbnail").toUrl().isLocalFile());
        QQuickItem *poster = h.item(QStringLiteral("previewFrame"));
        QVERIFY(poster->property("poster").toUrl().isLocalFile());

        // Use as background puts it on the Background layer, looping.
        QVERIFY(h.click(QStringLiteral("useAsBackground")));
        QCOMPARE(h.show.scene().layer(Layer::Background).content.kind, Content::Kind::Video);
        QCOMPARE(layerValue(*h.controller, QStringLiteral("Background")), QStringLiteral("Motion loop: loop-1080p-vp9"));

        // Back to Songs.
        QVERIFY(h.click(QStringLiteral("libraryTab-0")));
        h.settle();
        QVERIFY(h.item(QStringLiteral("importSongs"))->isVisible());
    }
};

QTEST_MAIN(TestMedia)
#include "tst_media.moc"
