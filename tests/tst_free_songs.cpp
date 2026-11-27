#include "freesongs.h"

#include "lumen/core/data/database.h"
#include "lumen/core/data/repository.h"
#include "lumen/core/songs/songlibrary.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace lumen;
using namespace lumen::core::data;

// The free songs every library starts with: all of them go in, with their
// chords and credits, and a second time adds nothing.
class TestFreeSongs : public QObject
{
    Q_OBJECT

private slots:
    void collectionGoesIn()
    {
        const QByteArray collection = app::builtInFreeSongs();
        QVERIFY(!collection.isEmpty());
        QCOMPARE(app::builtInFreeSongsVersion(), 1);
        const int count = int(QJsonDocument::fromJson(collection).object().value("songs").toArray().size());
        QVERIFY(count > 1000);

        // On disk, as a church's library is.
        QTemporaryDir dir;
        auto db = Database::open(dir.filePath(QStringLiteral("library.db")));
        QVERIFY(db.ok());
        core::songs::SongLibrary songs(*db.value());
        QElapsedTimer timer;
        timer.start();
        auto added = app::addFreeSongs(*db.value(), songs, collection);
        QVERIFY(added.ok());
        qInfo("%d of %d songs added in %lld ms", added.value(), count, timer.elapsed());
        QVERIFY2(added.value() >= count * 98 / 100, qPrintable(QString::number(added.value()))); // a few repeat a title and first line

        // Amazing Grace: its writer, public domain, chords kept off the words.
        auto hits = songs.search(QStringLiteral("amazing grace"));
        QVERIFY(hits.ok() && !hits.value().empty());
        const auto &hit = hits.value().front();
        QCOMPARE(hit.title, QStringLiteral("Amazing Grace"));
        QCOMPARE(hit.author, QStringLiteral("John Newton"));
        QCOMPARE(hit.copyright, QStringLiteral("Public domain"));
        QCOMPARE(hit.key, QStringLiteral("G"));
        const auto groups = Repository<Group>(*db.value()).listChildren(hit.presentationId).value();
        QVERIFY(!groups.empty());
        const auto slides = Repository<Slide>(*db.value()).listChildren(groups.front().id).value();
        QVERIFY2(slides.front().chords.startsWith(QStringLiteral("[G]Amazing grace!")), qPrintable(slides.front().chords));
        const auto elements = Repository<Element>(*db.value()).listChildren(slides.front().id).value();
        QVERIFY(!elements.front().content.contains(u'['));

        // A Creative Commons song credits its writer and licence.
        auto cc = songs.search(QStringLiteral("At the Dawning"));
        QVERIFY(cc.ok() && !cc.value().empty());
        QVERIFY2(cc.value().front().copyright.contains(QStringLiteral("CC BY")), qPrintable(cc.value().front().copyright));
        QVERIFY(cc.value().front().copyright.contains(QStringLiteral("Kathy Pluth")));
        // Other languages too.
        auto french = songs.search(QStringLiteral("Seigneur"));
        QVERIFY(french.ok() && !french.value().empty());

        // Again (a new version of Lumen starting): nothing twice.
        auto again = app::addFreeSongs(*db.value(), songs, collection);
        QVERIFY(again.ok());
        QCOMPARE(again.value(), 0);
    }
};

QTEST_MAIN(TestFreeSongs)
#include "tst_free_songs.moc"
