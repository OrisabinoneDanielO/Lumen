#include "lumen/core/data/database.h"
#include "lumen/core/data/repository.h"
#include "lumen/core/songs/foreignimport.h"
#include "lumen/core/songs/songlibrary.h"

#include <QFile>
#include <QTest>

using namespace lumen::core;
using namespace lumen::core::songs;

// Batch 17 "Done when": real libraries from each program import without
// errors. The libraries are test files made by tests/data/libraries/
// make_libraries.py in each program's layout (no real libraries were
// available): OpenLP, EasyWorship 6/7 and ProPresenter 5 and 6.
namespace {

const QString root = QStringLiteral(LUMEN_LIBRARIES_DIR);

QStringList labels(const ParsedSong &song)
{
    QStringList out;
    for (const ParsedSection &s : song.sections)
        out << s.label;
    return out;
}

const ParsedSong *byTitle(const std::vector<LibrarySong> &songs, const QString &title)
{
    for (const LibrarySong &s : songs) {
        if (s.song.title == title)
            return &s.song;
    }
    return nullptr;
}

} // namespace

class TestForeignImport : public QObject
{
    Q_OBJECT

private slots:
    void rtf()
    {
        const QByteArray text = "{\\rtf1\\ansi\\ansicpg1252{\\fonttbl{\\f0 Tahoma;}}{\\*\\generator Test;}"
                                "\\uc1\\pard Caf\\'e9 \\'97 \\u7865?yin\\par\r\nOl\\'faw\\u-28?a\\line done\\par}";
        // \u-28 is U+FFE4 written as a negative 16-bit number.
        QCOMPARE(rtfToText(text), QStringLiteral("Café — ẹyin\nOlúw￤a\ndone"));
        QCOMPARE(rtfToText("{\\rtf1 a\\{b\\}c\\\\}"), QStringLiteral("a{b}c\\"));
    }

    void openLp()
    {
        const QString path = root + QStringLiteral("/openlp/songs.sqlite");
        QCOMPARE(libraryKind(path), LibraryKind::OpenLP);
        auto songs = readOpenLP(path);
        QVERIFY2(songs.ok(), songs.ok() ? "" : qPrintable(songs.error().message));
        QCOMPARE(int(songs.value().size()), 3);

        const ParsedSong *blessed = byTitle(songs.value(), QStringLiteral("Blessed Assurance"));
        QVERIFY(blessed);
        QCOMPARE(labels(*blessed), (QStringList{"Verse 1", "Chorus", "Verse 2"}));
        QCOMPARE(blessed->order, (QStringList{"Verse 1", "Chorus", "Verse 2", "Chorus"}));
        QCOMPARE(int(blessed->sections[0].blocks.size()), 2); // split at [---]
        QCOMPARE(blessed->authors, QStringList{"Fanny Crosby"});
        QCOMPARE(blessed->ccli, QStringLiteral("22324"));

        const ParsedSong *friendSong = byTitle(songs.value(), QStringLiteral("What a Friend We Have in Jesus"));
        QVERIFY(friendSong);
        QCOMPARE(friendSong->authors, (QStringList{"Joseph M. Scriven", "Charles C. Converse"}));

        const ParsedSong *yoruba = byTitle(songs.value(), QStringLiteral("Ẹ Yin Olúwa"));
        QVERIFY(yoruba);
        QCOMPARE(labels(*yoruba), (QStringList{"Verse 1", "Chorus", "Bridge"}));
        QCOMPARE(yoruba->sections[0].blocks[0][0].text, QStringLiteral("Ẹ yin Olúwa, ẹ̀yin ènìyàn rẹ̀,"));
        QCOMPARE(yoruba->authors, QStringList{"Adé Ọlálékan"});
    }

    void easyWorship()
    {
        const QString path = root + QStringLiteral("/easyworship/Databases/Data/Songs.db");
        QCOMPARE(libraryKind(path), LibraryKind::EasyWorship);
        auto songs = readEasyWorship(path);
        QVERIFY2(songs.ok(), songs.ok() ? "" : qPrintable(songs.error().message));
        QCOMPARE(int(songs.value().size()), 2);

        const ParsedSong *well = byTitle(songs.value(), QStringLiteral("It Is Well with My Soul"));
        QVERIFY(well);
        QCOMPARE(labels(*well), (QStringList{"Verse 1", "Chorus", "Verse 2"}));
        QCOMPARE(int(well->sections[0].blocks.size()), 2); // a blank line: a new slide
        QCOMPARE(well->sections[0].blocks[0][0].text, QStringLiteral("When peace like a river attendeth my way,"));
        QCOMPARE(well->authors, (QStringList{"Horatio Spafford", "Philip Bliss"}));
        QCOMPARE(well->ccli, QStringLiteral("25376"));

        const ParsedSong *cafe = byTitle(songs.value(), QStringLiteral("Café Praise"));
        QVERIFY(cafe);
        QCOMPARE(cafe->copyright, QStringLiteral("© 2026 Lumen (admin. Lumen Music)"));
        QCOMPARE(cafe->sections[1].blocks[0][0].text, QStringLiteral("Ẹ yin Olúwa — praise the Lord!"));
    }

    void proPresenter()
    {
        QFile six(root + QStringLiteral("/propresenter/Come Thou Fount.pro6"));
        QVERIFY(six.open(QIODevice::ReadOnly));
        auto fount = parseProPresenter(six.readAll(), QStringLiteral("Come Thou Fount"));
        QVERIFY2(fount.ok(), fount.ok() ? "" : qPrintable(fount.error().message));
        QCOMPARE(fount.value().title, QStringLiteral("Come, Thou Fount of Every Blessing"));
        QCOMPARE(labels(fount.value()), (QStringList{"Verse 1", "Chorus", "Verse 2"}));
        QCOMPARE(fount.value().order, (QStringList{"Verse 1", "Chorus", "Verse 2", "Chorus"}));
        QCOMPARE(int(fount.value().sections[0].blocks.size()), 2);
        QCOMPARE(fount.value().copyright, QStringLiteral("1758 Public domain"));
        QCOMPARE(fount.value().ccli, QStringLiteral("108389"));
        QCOMPARE(fount.value().authors, QStringList{"Robert Robinson"});

        QFile five(root + QStringLiteral("/propresenter/Nearer My God.pro5"));
        QVERIFY(five.open(QIODevice::ReadOnly));
        auto nearer = parseProPresenter(five.readAll(), QStringLiteral("Nearer My God"));
        QVERIFY2(nearer.ok(), nearer.ok() ? "" : qPrintable(nearer.error().message));
        QCOMPARE(int(nearer.value().sections[0].blocks.size()), 2); // from RTF
        QCOMPARE(nearer.value().sections[0].blocks[1][0].text, QStringLiteral("E'en though it be a cross"));

        QVERIFY(!parseProPresenter("<html/>", QStringLiteral("x")).ok());
    }

    // Done when: each library imports without errors, and again without
    // duplicates.
    void librariesImportIntoLumen()
    {
        auto db = data::Database::open(data::Database::inMemory());
        QVERIFY(db.ok());
        SongLibrary library(*db.value());
        for (const QString &folder : {QStringLiteral("/openlp"), QStringLiteral("/easyworship"),
                                      QStringLiteral("/propresenter/Come Thou Fount.pro6"),
                                      QStringLiteral("/propresenter/Ọlọrun Tobi.pro6"),
                                      QStringLiteral("/propresenter/Nearer My God.pro5")}) {
            const ImportReport report = library.importFiles({root + folder});
            QVERIFY2(report.failed == 0 && report.imported > 0,
                     qPrintable(folder + QStringLiteral(": ") + report.summary() + QStringLiteral("\n")
                                + report.messages.join(u'\n')));
        }
        auto all = library.search(QString());
        QVERIFY(all.ok());
        QCOMPARE(int(all.value().size()), 3 + 2 + 3);

        // The lyrics are searchable, accents and tone marks or not.
        for (const char *word : {"Ebenezer", "olorun", "cafe", "foretaste"}) {
            auto hits = library.search(QLatin1String(word));
            QVERIFY2(hits.ok() && !hits.value().empty(), word);
        }

        // Importing the whole folder again adds nothing; the ProPresenter 7
        // stand-in is the one file that cannot be read, and says why.
        const ImportReport again = library.importFiles({root});
        QCOMPARE(again.imported, 0);
        QCOMPARE(again.skipped, 8);
        QCOMPARE(again.failed, 1);
        QVERIFY(again.messages.join(u'\n').contains(QStringLiteral("Modern Song.pro: ProPresenter 7")));
    }
};

QTEST_GUILESS_MAIN(TestForeignImport)
#include "tst_foreign_import.moc"
