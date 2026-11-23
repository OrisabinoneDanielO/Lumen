#include "lumen/core/data/repository.h"
#include "lumen/core/songs/songimport.h"
#include "lumen/core/songs/songlibrary.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace lumen::core;
using namespace lumen::core::data;
using namespace lumen::core::songs;

namespace {

QStringList labels(const ParsedSong &song)
{
    QStringList out;
    for (const auto &section : song.sections)
        out << section.label;
    return out;
}

std::vector<LyricLine> lines(int count)
{
    std::vector<LyricLine> out;
    for (int i = 1; i <= count; ++i)
        out.push_back({QString::number(i), {}});
    return out;
}

QList<int> sizes(const std::vector<std::vector<LyricLine>> &slides)
{
    QList<int> out;
    for (const auto &slide : slides)
        out << int(slide.size());
    return out;
}

ParsedSong parse(const QString &text)
{
    auto song = parseTextSong(text, QStringLiteral("Fallback"));
    if (!song.ok())
        qFatal("parse failed: %s", qPrintable(song.error().message));
    return song.value();
}

// A made-up test song in Yoruba, Igbo and Hausa (test data, not a real hymn).
const QString africanSong = QStringLiteral(
    "Title: Ẹ jẹ́ ká yin Olúwa\n"
    "Author: Ọ̀mọ̀wé Adéwálé\n\n"
    "Verse 1\nẸ jẹ́ ká yin Olúwa,\nỌba ògo, Ọba ìyè.\n\n"
    "Verse 2\nỌ dị mma, Chineke m,\nỊ dị ukwuu n'ezie.\n\n"
    "Verse 3\nƊaukaka ga Allah,\nƙauna ta ƴanci.\n");

} // namespace

class TestSongImport : public QObject
{
    Q_OBJECT

private slots:
    // ---- shared rules ---------------------------------------------------

    void sectionHeaders_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("label");
        QTest::newRow("verse") << "Verse 1" << "Verse 1";
        QTest::newRow("upper") << "VERSE 2" << "Verse 2";
        QTest::newRow("short colon") << "V3:" << "Verse 3";
        QTest::newRow("brackets") << "[Verse 4]" << "Verse 4";
        QTest::newRow("unnumbered") << "Verse" << "Verse";
        QTest::newRow("chorus") << "Chorus" << "Chorus";
        QTest::newRow("chorus 1") << "Chorus 1" << "Chorus";
        QTest::newRow("chorus 2") << "(Chorus 2)" << "Chorus 2";
        QTest::newRow("c") << "C:" << "Chorus";
        QTest::newRow("refrain") << "Refrain" << "Chorus";
        QTest::newRow("pre-chorus") << "Pre-Chorus" << "Pre-chorus";
        QTest::newRow("prechorus") << "prechorus" << "Pre-chorus";
        QTest::newRow("pc") << "PC" << "Pre-chorus";
        QTest::newRow("bridge") << "Bridge" << "Bridge";
        QTest::newRow("b2") << "B2" << "Bridge 2";
        QTest::newRow("tag") << "Tag" << "Tag";
        QTest::newRow("ending") << "Ending" << "Ending";
        QTest::newRow("outro") << "Outro:" << "Ending";
        QTest::newRow("intro") << "Intro" << "Intro";
        QTest::newRow("lyric") << "Verse of the day is here" << "";
        QTest::newRow("lyric 2") << "Chorus of angels singing" << "";
        QTest::newRow("empty") << "" << "";
    }
    void sectionHeaders()
    {
        QFETCH(QString, line);
        QFETCH(QString, label);
        QCOMPARE(sectionLabelFromHeader(line), label);
    }

    void titlesFromFileNames()
    {
        QCOMPARE(titleFromFileName("/songs/amazing_grace.txt"), QStringLiteral("Amazing Grace"));
        QCOMPARE(titleFromFileName("017 be-still my soul.txt"), QStringLiteral("Be Still My Soul"));
        QCOMPARE(titleFromFileName("3_doxology.txt"), QStringLiteral("Doxology"));
        QCOMPARE(titleFromFileName("1999.txt"), QStringLiteral("1999"));
    }

    void encodings()
    {
        const QString yoruba = QStringLiteral("Ẹ jẹ́ ká yin Olúwa");
        QCOMPARE(decodeText(yoruba.toUtf8()), yoruba);
        QCOMPARE(decodeText("\xEF\xBB\xBF" + yoruba.toUtf8()), yoruba);

        QByteArray utf16le("\xFF\xFE", 2);
        for (QChar c : yoruba) {
            utf16le.append(char(c.unicode() & 0xFF));
            utf16le.append(char(c.unicode() >> 8));
        }
        QCOMPARE(decodeText(utf16le), yoruba);

        QByteArray utf16be("\xFE\xFF", 2);
        for (QChar c : yoruba) {
            utf16be.append(char(c.unicode() >> 8));
            utf16be.append(char(c.unicode() & 0xFF));
        }
        QCOMPARE(decodeText(utf16be), yoruba);

        // Windows-1252: curly quotes, an em dash and an é (not valid UTF-8).
        QCOMPARE(decodeText("\x93Hallelujah\x94 \x97 caf\xE9"),
                 QStringLiteral("“Hallelujah” — café"));
    }

    void autoSplit_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("max");
        QTest::addColumn<QList<int>>("expected");
        QTest::newRow("none") << 0 << 2 << QList<int>{};
        QTest::newRow("1 at 2") << 1 << 2 << QList<int>{1};
        QTest::newRow("2 at 2") << 2 << 2 << QList<int>{2};
        QTest::newRow("3 at 2") << 3 << 2 << QList<int>{2, 1};
        QTest::newRow("4 at 2") << 4 << 2 << QList<int>{2, 2};
        QTest::newRow("5 at 2") << 5 << 2 << QList<int>{2, 2, 1};
        QTest::newRow("6 at 4: even") << 6 << 4 << QList<int>{3, 3};
        QTest::newRow("7 at 4") << 7 << 4 << QList<int>{4, 3};
        QTest::newRow("9 at 4") << 9 << 4 << QList<int>{3, 3, 3};
        QTest::newRow("max 0 means 1") << 2 << 0 << QList<int>{1, 1};
    }
    void autoSplit()
    {
        QFETCH(int, count);
        QFETCH(int, max);
        QFETCH(QList<int>, expected);
        const auto slides = splitIntoSlides(lines(count), max);
        QCOMPARE(sizes(slides), expected);
        // Order is kept.
        int next = 1;
        for (const auto &slide : slides) {
            for (const auto &line : slide)
                QCOMPARE(line.text, QString::number(next++));
        }
    }

    void searchNormalisation()
    {
        QCOMPARE(normaliseForSearch(QStringLiteral("Ẹ jẹ́ ká yin Olúwa!")), QStringLiteral("e je ka yin oluwa"));
        QCOMPARE(normaliseForSearch(QStringLiteral("Ọ dị mma, Chineke")), QStringLiteral("o di mma chineke"));
        QCOMPARE(normaliseForSearch(QStringLiteral("Ɗaukaka ƙauna ƴanci Ɓ")), QStringLiteral("daukaka kauna yanci b"));
        QCOMPARE(normaliseForSearch(QStringLiteral("  'Tis   GRACE—hath ")), QStringLiteral("tis grace hath"));
        QCOMPARE(normaliseForSearch(QStringLiteral("blessèd")), QStringLiteral("blessed"));
    }

    // ---- plain text -----------------------------------------------------

    void textDetailsAtTopAndBottom()
    {
        const ParsedSong song = parse(QStringLiteral(
            "Title: Test\nAuthor: A. Writer\nCCLI Number: 1234567\nKey: D\nTempo: 72\n\n"
            "Line one\nLine two\n\n"
            "CCLI Song # 7654321\n© 2001 Example Music\nFor use solely with the SongSelect Terms of Use.\n"
            "CCLI License # 99\n"));
        QCOMPARE(song.title, QStringLiteral("Test"));
        QCOMPARE(song.authors, QStringList{"A. Writer"});
        QCOMPARE(song.ccli, QStringLiteral("1234567")); // the top line wins
        QCOMPARE(song.key, QStringLiteral("D"));
        QCOMPARE(song.tempo, QStringLiteral("72"));
        QCOMPARE(song.copyright, QStringLiteral("2001 Example Music"));
        QCOMPARE(labels(song), QStringList{"Verse 1"});
        QCOMPARE(song.sections.front().texts(), (QStringList{"Line one", "Line two"}));
    }

    void textUnlabelledStanzasAreVerses()
    {
        // (Lyrics are words: lone letters such as "b" or "c" are section headers.)
        const ParsedSong song = parse(QStringLiteral("one\ntwo\n\nthree\nfour\n\nfive\n"));
        QCOMPARE(labels(song), (QStringList{"Verse 1", "Verse 2", "Verse 3"}));
        QCOMPARE(song.order, (QStringList{"Verse 1", "Verse 2", "Verse 3"}));
        QCOMPARE(song.title, QStringLiteral("Fallback"));
    }

    void textVerseNumbersFitAroundLabelledVerses()
    {
        const ParsedSong song = parse(QStringLiteral("one\n\nVerse 2\ntwo\n\nthree\n"));
        QCOMPARE(labels(song), (QStringList{"Verse 1", "Verse 2", "Verse 3"}));
    }

    void textHeaderOnlyRepeatsASection()
    {
        const ParsedSong song = parse(QStringLiteral(
            "Verse 1\nfirst words\n\nChorus\npraise him\n\nVerse 2\nsecond words\n\nChorus\n\nChorus\n"));
        QCOMPARE(labels(song), (QStringList{"Verse 1", "Chorus", "Verse 2"}));
        QCOMPARE(song.order, (QStringList{"Verse 1", "Chorus", "Verse 2", "Chorus", "Chorus"}));
    }

    void textHeaderThenBlankLineStillNamesTheStanza()
    {
        const ParsedSong song = parse(QStringLiteral("Chorus\n\nsing one\nsing two\n\nVerse 1\nwords here\n"));
        QCOMPARE(labels(song), (QStringList{"Chorus", "Verse 1"}));
        QCOMPARE(song.sections.front().texts(), (QStringList{"sing one", "sing two"}));
    }

    void textRepeatedStanzaBecomesChorus()
    {
        const ParsedSong song = parse(QStringLiteral("first\n\nsing on\nsing more\n\nsecond\n\nsing on\nsing more\n"));
        QCOMPARE(labels(song), (QStringList{"Verse 1", "Chorus", "Verse 2"}));
        QCOMPARE(song.order, (QStringList{"Verse 1", "Chorus", "Verse 2", "Chorus"}));
    }

    void textWrittenOutRepeatOfALabelledSection()
    {
        const ParsedSong same = parse(QStringLiteral("Chorus\npraise him\n\nVerse 1\nwords here\n\nChorus\npraise him\n"));
        QCOMPARE(labels(same), (QStringList{"Chorus", "Verse 1"}));
        QCOMPARE(same.order, (QStringList{"Chorus", "Verse 1", "Chorus"}));

        const ParsedSong different = parse(QStringLiteral("Chorus\npraise him\n\nChorus\nother words\n"));
        QCOMPARE(labels(different), (QStringList{"Chorus", "Chorus 2"}));
    }

    void textForcedSlideBreaks()
    {
        const ParsedSong song = parse(QStringLiteral("Verse 1\none\ntwo\nthree\n---\nfour\n"));
        QCOMPARE(song.sections.front().blocks.size(), std::size_t(2));
        QCOMPARE(song.sections.front().blocks[0].size(), std::size_t(3));
        QCOMPARE(song.sections.front().blocks[1].size(), std::size_t(1));
    }

    void textLoneFirstLineIsTheTitle()
    {
        const ParsedSong song = parse(QStringLiteral("My Song\n\nVerse 1\nwords here\n\nChorus\npraise him\n"));
        QCOMPARE(song.title, QStringLiteral("My Song"));
        QCOMPARE(labels(song), (QStringList{"Verse 1", "Chorus"}));
        // Without labelled sections a one-line first stanza is lyrics.
        QCOMPARE(parse(QStringLiteral("Alleluia\n\nAmen\n")).sections.size(), std::size_t(2));
    }

    void textWithoutLyricsFails()
    {
        auto song = parseTextSong(QStringLiteral("Title: Nothing\n\n"), QStringLiteral("x"));
        QVERIFY(!song.ok());
        QCOMPARE(song.error().code, Error::Code::Invalid);
        QVERIFY(!parseTextSong(QString(), QStringLiteral("x")).ok());
    }

    void textWindowsLineEndings()
    {
        const ParsedSong song = parse(QStringLiteral("Verse 1\r\none\r\ntwo\r\n\r\nChorus\r\nthree\r\n"));
        QCOMPARE(labels(song), (QStringList{"Verse 1", "Chorus"}));
        QCOMPARE(song.sections.front().texts(), (QStringList{"one", "two"}));
    }

    // ---- OpenLyrics -----------------------------------------------------

    void openLyricsOrderNamesAndWarnings()
    {
        auto song = parseOpenLyrics(R"(<?xml version="1.0"?>
<song xmlns="http://openlyrics.info/namespace/2009/song" version="0.9">
  <properties><titles><title>T</title></titles><verseOrder>v1 c p1 c2 b v1a x9 c</verseOrder></properties>
  <lyrics>
    <verse name="v1"><lines>a</lines></verse>
    <verse name="c"><lines>b</lines></verse>
    <verse name="p1"><lines>c</lines></verse>
    <verse name="c2"><lines>d</lines></verse>
    <verse name="b"><lines>e</lines></verse>
    <verse name="v1a"><lines>f</lines></verse>
  </lyrics>
</song>)");
        QVERIFY(song.ok());
        QCOMPARE(labels(song.value()),
                 (QStringList{"Verse 1", "Chorus", "Pre-chorus", "Chorus 2", "Bridge", "Verse 1a"}));
        QCOMPARE(song.value().order,
                 (QStringList{"Verse 1", "Chorus", "Pre-chorus", "Chorus 2", "Bridge", "Verse 1a", "Chorus"}));
        QCOMPARE(song.value().warnings.size(), 1); // x9
    }

    void openLyricsLinesChordsAndLanguages()
    {
        auto song = parseOpenLyrics(R"(<song xmlns="http://openlyrics.info/namespace/2009/song">
  <properties><titles><title>T</title></titles><ccliNo>123</ccliNo><key>A</key><tempo type="text">Slow</tempo></properties>
  <lyrics>
    <verse name="v1" lang="en">
      <lines><chord name="A"/>First <chord name="E"/>line<br/>Second <chord root="D" bass="F#">line</chord></lines>
      <lines>Third</lines>
    </verse>
    <verse name="v1" lang="fr"><lines>Première</lines></verse>
    <verse name="v2" lang="fr"><lines>Deuxième</lines></verse>
  </lyrics>
</song>)");
        QVERIFY(song.ok());
        const ParsedSong &s = song.value();
        QCOMPARE(s.ccli, QStringLiteral("123"));
        QCOMPARE(s.key, QStringLiteral("A"));
        QCOMPARE(s.tempo, QStringLiteral("Slow"));
        QCOMPARE(labels(s), QStringList{"Verse 1"}); // French verses skipped
        const auto &blocks = s.sections.front().blocks;
        QCOMPARE(blocks.size(), std::size_t(2));
        QCOMPARE(blocks[0][0], (LyricLine{"First line", "A E"}));
        QCOMPARE(blocks[0][1], (LyricLine{"Second line", "D/F#"}));
        QCOMPARE(blocks[1][0].text, QStringLiteral("Third"));
    }

    void openLyricsErrors()
    {
        QVERIFY(!parseOpenLyrics("<html><body/></html>").ok());
        QVERIFY(!parseOpenLyrics("not xml at all").ok());
        auto broken = parseOpenLyrics("<song><properties><titles><title>T</title></titles></properties><lyrics><verse name=\"v1\"><lines>a</verse></lyrics></song>");
        QVERIFY(!broken.ok());
        QVERIFY(broken.error().message.startsWith(QLatin1String("Line")));
        QVERIFY(!parseOpenLyrics("<song><properties><titles><title>T</title></titles></properties><lyrics/></song>").ok());
    }

    // ---- the song library -------------------------------------------------

    void addStoresTheWholeSong()
    {
        auto db = std::move(Database::open(Database::inMemory())).value();
        SongLibrary library(*db, 2);
        const ParsedSong song = parse(QStringLiteral(
            "Title: Stored\nAuthor: A\nCCLI: 42\n\nVerse 1\none\ntwo\nthree\n\nChorus\nfour\n\nChorus\n"));
        auto added = library.add(song);
        QVERIFY2(added.ok(), added.ok() ? "" : qPrintable(added.error().message));
        QVERIFY(!added.value().duplicate);

        auto presentation = Repository<Presentation>(*db).get(added.value().presentationId);
        QVERIFY(presentation.ok());
        QCOMPARE(presentation.value().kind, PresentationKind::Song);
        QCOMPARE(presentation.value().metadata.value("author").toString(), QStringLiteral("A"));
        QCOMPARE(presentation.value().metadata.value("ccliNumber").toString(), QStringLiteral("42"));

        auto groups = Repository<Group>(*db).listChildren(presentation.value().id);
        QCOMPARE(groups.value().size(), std::size_t(2));
        auto verseSlides = Repository<Slide>(*db).listChildren(groups.value()[0].id);
        QCOMPARE(verseSlides.value().size(), std::size_t(2)); // 3 lines at 2 per slide
        auto elements = Repository<Element>(*db).listChildren(verseSlides.value()[0].id);
        QCOMPARE(elements.value().front().content, QStringLiteral("one\ntwo"));

        auto arrangements = Repository<Arrangement>(*db).listChildren(presentation.value().id);
        QCOMPARE(arrangements.value().size(), std::size_t(1));
        QVERIFY(arrangements.value().front().isDefault);
        QCOMPARE(arrangements.value().front().groupIds,
                 (QStringList{groups.value()[0].id, groups.value()[1].id, groups.value()[1].id}));
    }

    void duplicatesAreSkipped()
    {
        auto db = std::move(Database::open(Database::inMemory())).value();
        SongLibrary library(*db);
        const ParsedSong song = parse(QStringLiteral("Title: Twice\n\nOnly line\n"));
        const QString first = library.add(song).value().presentationId;
        auto again = library.add(song);
        QVERIFY(again.ok());
        QVERIFY(again.value().duplicate);
        QCOMPARE(again.value().presentationId, first);
        // Same title, different words: a different song.
        QVERIFY(!library.add(parse(QStringLiteral("Title: Twice\n\nOther words\n"))).value().duplicate);
        QCOMPARE(Repository<Presentation>(*db).list().value().size(), std::size_t(2));
    }

    void searchFindsWordStartsIgnoringAccents()
    {
        auto db = std::move(Database::open(Database::inMemory())).value();
        SongLibrary library(*db);
        for (const QString &text : {QStringLiteral("Title: Amazing Grace\nAuthor: John Newton\n\nHow sweet the sound\n"),
                                    QStringLiteral("Title: Grace Alone\nAuthor: Someone\n\nAmazing love\n"),
                                    QStringLiteral("Title: Holy\nAuthor: Reginald Heber\nCCLI: 1156\n\nBlessèd Trinity\n"),
                                    africanSong})
            QVERIFY(library.add(parse(text)).ok());

        auto titles = [&library](const QString &query) {
            QStringList out;
            const auto hits = library.search(query);
            for (const auto &hit : hits.value())
                out << hit.title;
            return out;
        };
        QCOMPARE(titles(""), (QStringList{"Amazing Grace", "Grace Alone", "Holy", "Ẹ jẹ́ ká yin Olúwa"}));
        QCOMPARE(titles("gra"), (QStringList{"Amazing Grace", "Grace Alone"})); // both titles
        // Title matches come first: "amaz" is in one title and one lyric.
        QCOMPARE(titles("amaz"), (QStringList{"Amazing Grace", "Grace Alone"}));
        QCOMPARE(titles("sweet sou"), QStringList{"Amazing Grace"}); // every word must match
        QCOMPARE(titles("race"), QStringList{});                   // word starts only
        QCOMPARE(titles("newton"), QStringList{"Amazing Grace"});  // author
        QCOMPARE(titles("1156"), QStringList{"Holy"});             // CCLI number
        QCOMPARE(titles("BLESSED"), QStringList{"Holy"});          // accents and case
        QCOMPARE(titles("oluwa"), QStringList{"Ẹ jẹ́ ká yin Olúwa"});  // Yoruba, typed plainly
        QCOMPARE(titles("Olúwa"), QStringList{"Ẹ jẹ́ ká yin Olúwa"});  // Yoruba, with marks
        QCOMPARE(titles("chineke"), QStringList{"Ẹ jẹ́ ká yin Olúwa"}); // Igbo lyric
        QCOMPARE(titles("daukaka"), QStringList{"Ẹ jẹ́ ká yin Olúwa"}); // Hausa ɗ typed as d
    }

    void reindexAndIndexMissing()
    {
        auto db = std::move(Database::open(Database::inMemory())).value();
        SongLibrary library(*db);
        const QString id = library.add(parse(QStringLiteral("Title: Old Name\n\nWords\n"))).value().presentationId;

        // An edit made elsewhere shows up in search after reindex.
        auto presentation = Repository<Presentation>(*db).get(id).value();
        presentation.title = QStringLiteral("New Name");
        QVERIFY(Repository<Presentation>(*db).update(presentation).ok());
        QCOMPARE(library.search("new").value().size(), std::size_t(0));
        QVERIFY(library.reindex(id).ok());
        QCOMPARE(library.search("new").value().size(), std::size_t(1));

        // Songs made without the library (e.g. before this batch) get indexed.
        const auto libraryId = Repository<Library>(*db).list().value().front().id;
        const auto bare = Repository<Presentation>(*db).create(
            {.libraryId = libraryId, .kind = PresentationKind::Song, .title = "Unindexed"}).value();
        QCOMPARE(library.search("unindexed").value().size(), std::size_t(0));
        QVERIFY(library.indexMissing().ok());
        QCOMPARE(library.search("unindexed").value().size(), std::size_t(1));

        // Deleting a song removes its index entry.
        QVERIFY(Repository<Presentation>(*db).remove(bare.id).ok());
        QCOMPARE(library.search("unindexed").value().size(), std::size_t(0));
    }

    void importFilesReportsEachFile()
    {
        QTemporaryDir dir;
        auto write = [&dir](const QString &name, const QByteArray &bytes) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(bytes);
        };
        QDir(dir.path()).mkpath(QStringLiteral("more"));
        write("good.txt", "Title: Good\n\nLine\n");
        write("more/deeper.txt", "Deeper line\n");
        write("broken.xml", "<song><lyrics>");
        write("empty.txt", "\n\n");
        write("notes.pdf", "ignored: not a song type");

        auto db = std::move(Database::open(Database::inMemory())).value();
        SongLibrary library(*db);
        ImportReport report = library.importFiles({dir.path()});
        QCOMPARE(report.imported, 2);
        QCOMPARE(report.failed, 2);
        QCOMPARE(report.skipped, 0);
        QCOMPARE(report.summary(), QStringLiteral("2 imported, 2 failed"));
        QVERIFY(report.messages.join(u' ').contains(QLatin1String("broken.xml")));

        report = library.importFiles({dir.filePath("good.txt"), dir.filePath("missing.txt")});
        QCOMPARE(report.skipped, 1);
        QCOMPARE(report.failed, 1);
        QCOMPARE(report.summary(), QStringLiteral("0 imported, 1 skipped, 1 failed"));
    }

    // A church-sized library searches in a few milliseconds.
    void searchIsFast()
    {
        auto db = std::move(Database::open(Database::inMemory())).value();
        SongLibrary library(*db);
        QVERIFY(db->inTransaction([&]() -> Status {
            for (int i = 0; i < 1000; ++i) {
                auto added = library.add(parse(QStringLiteral(
                    "Title: Song number %1\nAuthor: Writer %2\n\nVerse %1 has words of praise\n"
                    "and lines of worship and grace\n").arg(i).arg(i % 37)));
                if (!added.ok())
                    return added.error();
            }
            return success();
        }).ok());

        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 10; ++i)
            QVERIFY(library.search(QStringLiteral("wor gra %1").arg(i)).ok());
        const double perSearch = double(timer.elapsed()) / 10.0;
        qInfo("Search over 1000 songs: %.1f ms", perSearch);
        QVERIFY2(perSearch < 50.0, qPrintable(QString::number(perSearch)));
    }
};

QTEST_GUILESS_MAIN(TestSongImport)
#include "tst_song_import.moc"
