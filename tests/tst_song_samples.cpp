#include "operatorharness.h"

#include "lumen/core/data/repository.h"
#include "lumen/core/songs/songlibrary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

using namespace lumen;
using namespace lumen::core;
using namespace lumen::core::data;
using lumen::test::OperatorHarness;

// Batch 6 "Done when": 20 sample songs import and display correctly.
//
// tests/data/songs holds 20 public-domain hymns in different layouts: labelled
// and unlabelled plain text, bracketed and abbreviated headers, repeat
// markers, a chorus written out twice, a CCLI-style title line and footer,
// forced slide breaks, UTF-16 and Windows-1252 files, and OpenLyrics with
// verse orders, chords, several languages and split <lines>. expected.json
// says what each should become at 2 lines per slide.
//
// The test imports the folder, checks every song in the library, then opens
// each one in the real operator window and checks the slide grid.
class TestSongSamples : public QObject
{
    Q_OBJECT

private:
    QJsonArray m_expected;
    std::unique_ptr<OperatorHarness> m_ui;

    static QStringList strings(const QJsonValue &value)
    {
        QStringList out;
        for (const QJsonValue &v : value.toArray())
            out << v.toString();
        return out;
    }

    QString idForTitle(const QString &title)
    {
        auto hits = m_ui->songs->search({});
        for (const auto &hit : hits.value()) {
            if (hit.title == title)
                return hit.presentationId;
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        QFile file(QStringLiteral(LUMEN_SONGS_DIR "/expected.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        m_expected = QJsonDocument::fromJson(file.readAll()).array();
        QCOMPARE(m_expected.size(), 20);

        // The real window over an empty library, then import the folder.
        auto db = Database::open(Database::inMemory());
        QVERIFY(db.ok());
        m_ui = std::make_unique<OperatorHarness>();
        QString error;
        QVERIFY2(m_ui->load(false, &error, std::move(db).value()), qPrintable(error));
        QCOMPARE(m_ui->songs->linesPerSlide(), 2);

        const QString summary = m_ui->controller->importSongs({QUrl::fromLocalFile(LUMEN_SONGS_DIR)});
        QVERIFY2(m_ui->controller->importDetails().isEmpty(), qPrintable(m_ui->controller->importDetails()));
        QCOMPARE(summary, QStringLiteral("20 imported"));
        QCOMPARE(m_ui->controller->songs()->property("count").toInt(), 20);
    }

    void cleanupTestCase() { m_ui.reset(); }

    void songsAreStoredCorrectly_data()
    {
        QTest::addColumn<QJsonObject>("song");
        for (const QJsonValue &song : m_expected)
            QTest::newRow(qPrintable(song.toObject().value("file").toString())) << song.toObject();
    }

    void songsAreStoredCorrectly()
    {
        QFETCH(QJsonObject, song);
        const QString title = song.value("title").toString();
        const QString id = idForTitle(title);
        QVERIFY2(!id.isEmpty(), qPrintable("not in the library: " + title));

        Database &db = m_ui->database();
        const Presentation presentation = Repository<Presentation>(db).get(id).value();
        QCOMPARE(presentation.kind, PresentationKind::Song);
        const QJsonObject &meta = presentation.metadata;
        QCOMPARE(meta.value("author").toString(), song.value("author").toString());
        for (const char *field : {"copyright", "key", "tempo"}) {
            if (song.contains(QLatin1String(field)))
                QCOMPARE(meta.value(QLatin1String(field)).toString(), song.value(QLatin1String(field)).toString());
        }

        // Sections (groups) in order, and the default arrangement.
        const auto groups = Repository<Group>(db).listChildren(id).value();
        QStringList labels;
        for (const Group &group : groups)
            labels << group.label;
        QCOMPARE(labels, strings(song.value("sections")));

        const auto arrangements = Repository<Arrangement>(db).listChildren(id).value();
        QCOMPARE(arrangements.size(), std::size_t(1));
        QStringList order;
        for (const QString &groupId : arrangements.front().groupIds) {
            for (const Group &group : groups) {
                if (group.id == groupId)
                    order << group.label;
            }
        }
        QCOMPARE(order, strings(song.value("order")));

        // No slide holds more than 2 lines.
        for (const Group &group : groups) {
            const auto slides = Repository<Slide>(db).listChildren(group.id);
            for (const Slide &slide : slides.value()) {
                const auto elements = Repository<Element>(db).listChildren(slide.id).value();
                QCOMPARE(elements.size(), std::size_t(1));
                QVERIFY(elements.front().content.count(u'\n') <= 1);
            }
        }

        if (song.contains("firstChords")) {
            const auto slides = Repository<Slide>(db).listChildren(groups.front().id).value();
            QCOMPARE(slides.front().chords, song.value("firstChords").toString());
        }
    }

    void songsDisplayCorrectly_data() { songsAreStoredCorrectly_data(); }

    void songsDisplayCorrectly()
    {
        QFETCH(QJsonObject, song);
        const QString title = song.value("title").toString();

        // Find the song through the library search box, as an operator would,
        // and click its row.
        auto *search = m_ui->item("searchInput");
        QVERIFY(search);
        search->setProperty("text", title);
        QVERIFY2(m_ui->clickSongRow(title), qPrintable("search did not show " + title));

        // The slide grid shows the song: title, arrangement and every slide.
        QCOMPARE(m_ui->controller->itemTitle(), title);
        QCOMPARE(m_ui->controller->arrangement().size(), strings(song.value("order")).size());
        const int slides = song.value("slides").toInt();
        QCOMPARE(m_ui->controller->slides()->property("count").toInt(), slides);
        QTRY_VERIFY(m_ui->item(QStringLiteral("slide-%1").arg(slides - 1)));
        QVERIFY(!m_ui->item(QStringLiteral("slide-%1").arg(slides)));
        auto *first = m_ui->item("slide-0");
        QCOMPARE(first->property("slideText").toString(), song.value("firstSlide").toString());
        QVERIFY(first->property("groupLabel").toString().startsWith(strings(song.value("order")).front()));
        QVERIFY(first->height() > 0 && first->isVisible());

        // And it goes live.
        QVERIFY(m_ui->click("slide-0"));
        QCOMPARE(m_ui->controller->liveText(), song.value("firstSlide").toString());
        m_ui->key(Qt::Key_Escape);
    }

    // Set LUMEN_SCREENSHOT_DIR to save the window with the imported library.
    void screenshot()
    {
        const QString dir = qEnvironmentVariable("LUMEN_SCREENSHOT_DIR");
        if (dir.isEmpty())
            QSKIP("LUMEN_SCREENSHOT_DIR not set");
        m_ui->item("searchInput")->setProperty("text", QStringLiteral("grace"));
        QVERIFY(m_ui->clickSongRow(QStringLiteral("Amazing Grace")));
        QVERIFY(m_ui->click("slide-2"));
        m_ui->settle();
        QTest::qWait(100);
        QVERIFY(m_ui->window->grabWindow().save(dir + QStringLiteral("/library.png")));
    }

    void importingAgainSkipsEverySong()
    {
        const QString summary = m_ui->controller->importSongs({QUrl::fromLocalFile(LUMEN_SONGS_DIR)});
        QCOMPARE(summary, QStringLiteral("0 imported, 20 skipped"));
        QCOMPARE(m_ui->songs->search({}).value().size(), std::size_t(20));
    }

    void addToPlanCreatesTodaysPlan()
    {
        m_ui->item("searchInput")->setProperty("text", QString());
        QTRY_COMPARE(m_ui->controller->songs()->property("count").toInt(), 20);
        QVERIFY(m_ui->clickSongRow(QStringLiteral("A Mighty Fortress Is Our God")));
        QVERIFY(m_ui->click("addToPlan"));
        QCOMPARE(m_ui->controller->plan()->property("count").toInt(), 1);
        QCOMPARE(m_ui->controller->planName(), QStringLiteral("Sunday Service"));
        // It is stored: a fresh load of the latest plan has it.
        auto plan = app::loadLatestPlan(m_ui->database());
        QVERIFY(plan.ok() && plan.value());
        QCOMPARE(plan.value()->items.size(), std::size_t(1));
        QCOMPARE(plan.value()->items.front().title, QStringLiteral("A Mighty Fortress Is Our God"));
    }
};

QTEST_MAIN(TestSongSamples)
#include "tst_song_samples.moc"
