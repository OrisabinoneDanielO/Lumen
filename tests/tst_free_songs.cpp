#include "freesongs.h"

#include "lumen/core/songs/chords.h"
#include "lumen/core/songs/songimport.h"

#include "lumen/core/data/database.h"
#include "lumen/core/data/repository.h"
#include "lumen/core/songs/songlibrary.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
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
    // Every lyric line of every song arrives (only section headers and
    // chord-only lines are left out).
    void noLyricsLost()
    {
        const QJsonArray all = QJsonDocument::fromJson(app::builtInFreeSongs()).object().value("songs").toArray();
        QStringList short_;
        int checked = 0;
        for (const QJsonValue &value : all) {
            const QJsonObject s = value.toObject();
            const QString body = s.value("body").toString();
            const bool openLyrics = s.value("format").toString() == QLatin1String("openlyrics");
            auto parsed = openLyrics ? core::songs::parseOpenLyrics(body.toUtf8())
                                     : core::songs::parseTextSong(body, s.value("title").toString());
            if (!parsed.ok()) {
                short_ << QStringLiteral("%1 [%2]: not read: %3").arg(s.value("title").toString(), s.value("id").toString(), parsed.error().message);
                continue;
            }
            int kept = 0;
            for (const auto &section : parsed.value().sections) {
                // What plays is the order: a section left out of it never shows.
                if (parsed.value().order.contains(section.label))
                    kept += section.lineCount();
            }
            // Sections repeated in the text (a chorus written out again) are
            // stored once: count each label's lines once in the source too.
            int expected = 0;
            if (openLyrics) {
                expected = int(body.count(QStringLiteral("<br/>"))) + int(body.count(QStringLiteral("</lines>")));
            } else {
                QSet<QString> seen;
                QString label;
                bool counting = true;
                for (const QString &raw : body.split(u'\n')) {
                    const QString line = raw.trimmed();
                    if (line.isEmpty())
                        continue;
                    const QString header = core::songs::sectionLabelFromHeader(line);
                    if (!header.isEmpty()) {
                        counting = !seen.contains(header);
                        seen.insert(header);
                        continue;
                    }
                    if (core::songs::isChordLine(line))
                        continue;
                    if (counting)
                        ++expected;
                }
            }
            ++checked;
            if (kept < expected)
                short_ << QStringLiteral("%1 [%2]: %3 of %4").arg(s.value("title").toString(), s.value("id").toString()).arg(kept).arg(expected);
        }
        qInfo("%d songs checked, %lld short", checked, qlonglong(short_.size()));
        for (const QString &line : short_.mid(0, 40))
            qInfo("  %s", qPrintable(line));
        QVERIFY2(short_.isEmpty(), qPrintable(QString::number(short_.size()) + " songs lose lines"));
    }

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
