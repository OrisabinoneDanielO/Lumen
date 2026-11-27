#include "lumen/core/scripture/bible.h"
#include "lumen/core/scripture/biblestore.h"
#include "lumen/core/scripture/reference.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace lumen::core;
using namespace lumen::core::scripture;

Q_DECLARE_METATYPE(lumen::core::scripture::Reference)

namespace QTest {
template <>
char *toString(const Reference &r)
{
    return qstrdup(QStringLiteral("%1 %2:%3-%4:%5").arg(r.book).arg(r.chapter).arg(r.verse).arg(r.endChapter).arg(r.endVerse).toUtf8());
}
} // namespace QTest

namespace {

// A small USFM book exercising the markup the bundled Bibles use.
const QString johnUsfm = QStringLiteral(
    "\\id JHN 43-JHN-test.sfm Test Bible\n"
    "\\h Johanu\n\\toc1 Ìwé Ìyìnrere Johanu\n\\toc2 Johanu\n\\toc3 Jh\n"
    "\\mt1 Johanu\n"
    "\\c 3\n"
    "\\s1 Jesus Teaches Nicodemus\n"
    "\\p\n"
    "\\v 16 \\w For|strong=\"G1063\"\\w* \\w God|strong=\"G2316\"\\w* so loved\\f + \\fr 3:16 \\ft A footnote.\\f* the world,\n"
    "\\q1 that he gave\\x - \\xo 3:16 \\xt Rom 5:8\\x* his \\wj one and only\\wj* Son.\n"
    "\\v 17-18 Verses \\add combined\\add* here.\n"
    "\\qc ב BETH.\n"
    "\\s2 A heading between verses\n"
    "\\qa Aleph\n"
    "\\p \\v 19 ¶ This is the judgment, \\+w qu|strong=\"G1\"\\+w*’\\+w il|strong=\"G2\"\\+w* came.\n"
    "\\c 4\n\\p\n\\v 1 Chapter four.\n");

Reference ref(const char *book, int c, int v, int ec, int ev)
{
    return {QLatin1String(book), c, v, ec, ev};
}

} // namespace

class TestScripture : public QObject
{
    Q_OBJECT

private:
    BookNames m_names;

private slots:
    // Other programs' Bible formats (OpenLP, OpenSong), so a church can bring
    // a Bible it may use. Public-domain KJV verses as samples.
    void zefaniaXml()
    {
        const auto books = parseZefania(QByteArrayLiteral(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?><XMLBIBLE biblename=\"KJV\">"
            "<BIBLEBOOK bnumber=\"1\" bname=\"Genesis\" bsname=\"Gen\"><CHAPTER cnumber=\"1\">"
            "<VERS vnumber=\"1\">In the beginning God created the heaven and the earth.</VERS>"
            "<VERS vnumber=\"2\">And the earth was without form, <NOTE>a note</NOTE>and void.</VERS></CHAPTER></BIBLEBOOK>"
            "<BIBLEBOOK bnumber=\"43\" bname=\"John\"><CHAPTER cnumber=\"3\"><VERS vnumber=\"16\">For God so loved the world.</VERS>"
            "</CHAPTER></BIBLEBOOK></XMLBIBLE>"));
        QVERIFY(books.ok());
        QCOMPARE(int(books.value().size()), 2);
        QCOMPARE(books.value()[0].code, QStringLiteral("GEN"));
        QCOMPARE(books.value()[0].abbreviation, QStringLiteral("Gen"));
        QCOMPARE(books.value()[0].chapters.at(1).at(1).text, QStringLiteral("And the earth was without form, and void."));
        QCOMPARE(books.value()[1].code, QStringLiteral("JHN"));
        QCOMPARE(books.value()[1].chapters.at(3).at(0).number, 16);
    }

    void openSongXml()
    {
        // Latin-1, as older OpenSong files are; books by name, else place.
        QByteArray xml("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><bible>"
                       "<b n=\"Genesis\"><c n=\"1\"><v n=\"1\">In the beginning God created the heaven and the earth.</v></c></b>"
                       "<b n=\"Exodus\"><c n=\"1\"><v n=\"1\">Now these are the names \xe9.</v></c></b></bible>");
        const auto books = parseOpenSong(xml);
        QVERIFY(books.ok());
        QCOMPARE(int(books.value().size()), 2);
        QCOMPARE(books.value()[1].code, QStringLiteral("EXO"));
        QCOMPARE(books.value()[1].chapters.at(1).at(0).text, QStringLiteral("Now these are the names é."));
        // Names in another language: by place.
        const auto yoruba = parseOpenSong(QByteArrayLiteral("<bible><b n=\"Genesisi\"><c n=\"1\"><v n=\"1\">Ní atetekọṣe</v></c></b>"
                                                            "<b n=\"Eksodu\"><c n=\"1\"><v n=\"1\">Wọnyi</v></c></b></bible>"));
        QVERIFY(yoruba.ok());
        QCOMPARE(yoruba.value()[1].code, QStringLiteral("EXO"));
        QCOMPARE(yoruba.value()[1].name, QStringLiteral("Eksodu"));
    }

    void openLpCsv()
    {
        const QByteArray books("1,1,\"Genesisi\",\"Gen\"\n43,2,\"Johanu\",\"Joh\"\n");
        // A verse number left at the start, and a line break inside quotes,
        // as one converter writes them.
        const QByteArray verses("Genesisi,1,1,\"\n1 Ní atetekọṣe Ọlọrun dá ọrun ati aiye\"\n"
                                "Johanu,3,16,\" 16 Nitori Ọlọrun fẹ araiye tobẹ, ti o fi Ọmọ bibi rẹ̀ kanṣoṣo funni\"\n"
                                "43,3,17,\"Kò ran Ọmọ rẹ̀, \"\"to say\"\", wá aiye\"\n");
        const auto parsed = parseOpenLpCsv(books, verses);
        QVERIFY(parsed.ok());
        QCOMPARE(int(parsed.value().size()), 2);
        QCOMPARE(parsed.value()[0].chapters.at(1).at(0).text, QStringLiteral("Ní atetekọṣe Ọlọrun dá ọrun ati aiye"));
        QCOMPARE(parsed.value()[1].code, QStringLiteral("JHN"));
        QCOMPARE(parsed.value()[1].name, QStringLiteral("Johanu"));
        QVERIFY(parsed.value()[1].chapters.at(3).at(0).text.startsWith(QStringLiteral("Nitori Ọlọrun")));
        QCOMPARE(parsed.value()[1].chapters.at(3).at(1).text, QStringLiteral("Kò ran Ọmọ rẹ̀, \"to say\", wá aiye"));

        // Through the same door as USFM: the folder, or either file; an
        // OpenSong file with no .xml ending.
        QTemporaryDir dir;
        QFile b(dir.filePath("books.csv")), v(dir.filePath("verses.csv"));
        QVERIFY(b.open(QIODevice::WriteOnly) && b.write(books) > 0);
        QVERIFY(v.open(QIODevice::WriteOnly) && v.write(verses) > 0);
        b.close();
        v.close();
        QCOMPARE(int(readBibleFiles(dir.path()).value().size()), 2);
        QCOMPARE(int(readBibleFiles(dir.filePath("verses.csv")).value().size()), 2);
        QFile song(dir.filePath("KingJames.xmm"));
        QVERIFY(song.open(QIODevice::WriteOnly));
        song.write("<bible><b n=\"John\"><c n=\"3\"><v n=\"16\">For God so loved the world.</v></c></b></bible>");
        song.close();
        const auto fromFile = readBibleFiles(dir.filePath("KingJames.xmm"));
        QVERIFY(fromFile.ok());
        QCOMPARE(fromFile.value().front().code, QStringLiteral("JHN"));
    }

    // LUMEN_TRY_BIBLES="a;b": reads real files (not in the repository) and
    // says what they hold.
    void tryBibles()
    {
        const QString list = qEnvironmentVariable("LUMEN_TRY_BIBLES");
        if (list.isEmpty())
            QSKIP("LUMEN_TRY_BIBLES not set");
        for (const QString &path : list.split(u';', Qt::SkipEmptyParts)) {
            const auto books = readBibleFiles(path);
            QVERIFY2(books.ok(), qPrintable(path + ": " + (books.ok() ? QString() : books.error().message)));
            int verses = 0;
            QString john;
            for (const ParsedBook &book : books.value()) {
                verses += book.verseCount();
                if (book.code == QLatin1String("JHN") && book.chapters.count(3) && book.chapters.at(3).size() >= 16)
                    john = book.name + " 3:16 " + book.chapters.at(3).at(15).text.left(60);
            }
            qInfo("%s: %d books, %d verses; %s", qPrintable(QFileInfo(path).fileName()), int(books.value().size()), verses, qPrintable(john));
        }
    }

    // Typing ahead: "J" suggests John 1:1, Space takes the book, then the
    // chapter, then the verse; the most read book wins among those that fit.
    void typingAhead()
    {
        const std::vector<std::pair<QString, QString>> books{
            {"GEN", "Genesis"}, {"EXO", "Exodus"}, {"JOS", "Joshua"}, {"JDG", "Judges"}, {"JOB", "Job"},
            {"PSA", "Psalms"}, {"JER", "Jeremiah"}, {"JOL", "Joel"}, {"JON", "Jonah"}, {"MAT", "Matthew"},
            {"JHN", "John"}, {"ACT", "Acts"}, {"ROM", "Romans"}, {"1CO", "1 Corinthians"}, {"JAS", "James"},
            {"1JN", "1 John"}, {"JUD", "Jude"}};
        const BookNames names;
        const auto check = [&](const QString &typed, const QString &suggestion, const QString &step) {
            const Completion c = completeReference(typed, names, books);
            QVERIFY2(c.suggestion == suggestion && c.step == step,
                     qPrintable(QStringLiteral("\"%1\" gave \"%2\" / \"%3\"").arg(typed, c.suggestion, c.step)));
        };
        check("j", "John 1:1", "John ");
        check("J", "John 1:1", "John ");
        check("jo", "John 1:1", "John ");
        check("job", "Job 1:1", "Job ");
        check("josh", "Joshua 1:1", "Joshua ");
        check("jn", "John 1:1", "John ");
        check("p", "Psalms 1:1", "Psalms ");
        check("1 c", "1 Corinthians 1:1", "1 Corinthians ");
        check("1 j", "1 John 1:1", "1 John ");
        // Space after the whole name: on to the chapter (1 unless typed).
        check("John ", "John 1:1", "John 1:");
        check("John 3", "John 3:1", "John 3:");
        check("jn 3", "John 3:1", "John 3:");
        check("John 3:", "John 3:1", "John 3:1");
        check("John 1:", "John 1:1", "John 1:1");
        // A verse or a range being typed, or nothing that matches: nothing.
        check("John 3:16", "", "");
        check("jn 3 16-18", "", "");
        check("xyz", "", "");
        check("", "", "");
        check("3", "", "");
        // A translation's own (accented) names.
        const std::vector<std::pair<QString, QString>> yoruba{{"GEN", "Jẹ́nẹ́sísì"}, {"JHN", "Jòhánù"}, {"JOB", "Jóòbù"}};
        const Completion y = completeReference(QStringLiteral("joh"), names, yoruba);
        QCOMPARE(y.suggestion, QStringLiteral("Jòhánù 1:1"));
    }

    void initTestCase()
    {
        // A translation's own names, as the bundled Yoruba Bible has them.
        m_names.add(QStringLiteral("Johanu"), QStringLiteral("JHN"));
        m_names.add(QStringLiteral("Ìwé Ìyìnrere Johanu"), QStringLiteral("JHN"));
        m_names.add(QStringLiteral("1 Kọ́ríńtì"), QStringLiteral("1CO"));
        m_names.add(QStringLiteral("Yohanna"), QStringLiteral("JHN"));
    }

    // ---- references -------------------------------------------------------

    void references_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<Reference>("expected");
        QTest::newRow("jn 3 16-18") << "jn 3 16-18" << ref("JHN", 3, 16, 3, 18);
        QTest::newRow("John 3:16–18") << "John 3:16–18" << ref("JHN", 3, 16, 3, 18);
        QTest::newRow("john 3.16") << "john 3.16" << ref("JHN", 3, 16, 3, 16);
        QTest::newRow("JN. 3:16") << "JN. 3:16" << ref("JHN", 3, 16, 3, 16);
        QTest::newRow("no dash") << "jn 3 16 18" << ref("JHN", 3, 16, 3, 18);
        QTest::newRow("whole chapter") << "ps 23" << ref("PSA", 23, 0, 23, 0);
        QTest::newRow("psalm") << "Psalm 23:1" << ref("PSA", 23, 1, 23, 1);
        QTest::newRow("chapters") << "John 3-4" << ref("JHN", 3, 0, 4, 0);
        QTest::newRow("across chapters") << "gen 1:1-2:3" << ref("GEN", 1, 1, 2, 3);
        QTest::newRow("to chapter end") << "rom 8:28-" << ref("ROM", 8, 28, 8, 999);
        QTest::newRow("prefix") << "rom 8 28-30" << ref("ROM", 8, 28, 8, 30);
        QTest::newRow("full prefix") << "Revelat 21:4" << ref("REV", 21, 4, 21, 4);
        QTest::newRow("1 cor") << "1 cor 13" << ref("1CO", 13, 0, 13, 0);
        QTest::newRow("1cor") << "1cor 13:4-7" << ref("1CO", 13, 4, 13, 7);
        QTest::newRow("I Cor") << "I Cor 13" << ref("1CO", 13, 0, 13, 0);
        QTest::newRow("First") << "First Corinthians 13" << ref("1CO", 13, 0, 13, 0);
        QTest::newRow("2nd kings") << "2nd Kings 2:11" << ref("2KI", 2, 11, 2, 11);
        QTest::newRow("1 john") << "1 John 4:8" << ref("1JN", 4, 8, 4, 8);
        QTest::newRow("song of songs") << "Song of Songs 2:4" << ref("SNG", 2, 4, 2, 4);
        QTest::newRow("jude verse") << "jude 3" << ref("JUD", 1, 3, 1, 3);
        QTest::newRow("jude range") << "Jude 24-25" << ref("JUD", 1, 24, 1, 25);
        QTest::newRow("jude 1:3") << "Jude 1:3" << ref("JUD", 1, 3, 1, 3);
        QTest::newRow("jude whole") << "Jude" << ref("JUD", 1, 0, 1, 0);
        QTest::newRow("philemon") << "phm 6" << ref("PHM", 1, 6, 1, 6);
        QTest::newRow("yoruba") << "Johanu 3:16" << ref("JHN", 3, 16, 3, 16);
        QTest::newRow("yoruba plain") << "johanu 3 16" << ref("JHN", 3, 16, 3, 16);
        QTest::newRow("yoruba long") << "Ìwé Ìyìnrere Johanu 3:16" << ref("JHN", 3, 16, 3, 16);
        QTest::newRow("yoruba 1 cor") << "1 Korinti 13:4" << ref("1CO", 13, 4, 13, 4);
        QTest::newRow("hausa") << "Yohanna 3:16" << ref("JHN", 3, 16, 3, 16);
    }
    void references()
    {
        QFETCH(QString, text);
        QFETCH(Reference, expected);
        auto parsed = parseReference(text, m_names);
        QVERIFY2(parsed.ok(), parsed.ok() ? "" : qPrintable(parsed.error().message));
        QCOMPARE(parsed.value(), expected);
    }

    void badReferences_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << "";
        QTest::newRow("unknown book") << "Hezekiah 3:1";
        QTest::newRow("ambiguous") << "ph 2"; // Philippians or Philemon
        QTest::newRow("no chapter") << "John";
        QTest::newRow("backwards") << "John 3:18-16";
        QTest::newRow("two ranges") << "John 3:16-17-18";
        QTest::newRow("zero") << "John 0:1";
        QTest::newRow("words") << "John three";
    }
    void badReferences()
    {
        QFETCH(QString, text);
        auto parsed = parseReference(text, m_names);
        QVERIFY(!parsed.ok());
        QVERIFY(!parsed.error().message.isEmpty());
    }

    // Regression (batch 7): one bundled Bible abbreviates Jonah as "Jn",
    // which used to turn "jn 3 16" into Jonah.
    void translationNamesCannotOverrideEnglish()
    {
        BookNames names;
        names.add(QStringLiteral("Jn"), QStringLiteral("JON"));
        QCOMPARE(names.find("jn"), QStringLiteral("JHN"));
        // Two translations disagreeing on a name: neither wins.
        names.add(QStringLiteral("Yh"), QStringLiteral("JHN"));
        names.add(QStringLiteral("Yh"), QStringLiteral("JON"));
        QCOMPARE(names.find("yh"), QString());
        // Igbo's John is "Jọn": with its accent it means John, while plain
        // "jon" stays the English abbreviation for Jonah.
        names.add(QStringLiteral("Jọn"), QStringLiteral("JHN"));
        QCOMPARE(names.find(QStringLiteral("Jọn")), QStringLiteral("JHN"));
        QCOMPARE(names.find(QStringLiteral("jon")), QStringLiteral("JON"));
        // Agreeing is fine.
        names.add(QStringLiteral("Yohana"), QStringLiteral("JHN"));
        names.add(QStringLiteral("Yohana"), QStringLiteral("JHN"));
        QCOMPARE(names.find("yohana"), QStringLiteral("JHN"));
    }

    void formatting()
    {
        QCOMPARE(formatReference(ref("JHN", 3, 16, 3, 16), "John"), QStringLiteral("John 3:16"));
        QCOMPARE(formatReference(ref("JHN", 3, 16, 3, 18), "John"), QStringLiteral("John 3:16–18"));
        QCOMPARE(formatReference(ref("GEN", 1, 1, 2, 3), "Genesis"), QStringLiteral("Genesis 1:1–2:3"));
        QCOMPARE(formatReference(ref("PSA", 23, 0, 23, 0), "Psalms"), QStringLiteral("Psalms 23"));
        QCOMPARE(formatReference(ref("JHN", 3, 0, 4, 0), "Johanu"), QStringLiteral("Johanu 3–4"));
    }

    // ---- reading Bible files ------------------------------------------------

    void usfmBook()
    {
        auto book = parseUsfmBook(johnUsfm);
        QVERIFY(book.ok());
        const ParsedBook &b = book.value();
        QCOMPARE(b.code, QStringLiteral("JHN"));
        QCOMPARE(b.name, QStringLiteral("Johanu"));
        QCOMPARE(b.longName, QStringLiteral("Ìwé Ìyìnrere Johanu"));
        QCOMPARE(b.abbreviation, QStringLiteral("Jh"));
        QCOMPARE(b.chapters.size(), std::size_t(2));

        const auto &three = b.chapters.at(3);
        QCOMPARE(three.size(), std::size_t(3));
        // Strong's numbers, footnotes and cross-references gone; poetry and
        // words of Jesus kept as text.
        QCOMPARE(three[0], (Verse{16, 16, "For God so loved the world, that he gave his one and only Son."}));
        QCOMPARE(three[1], (Verse{17, 18, "Verses combined here."}));
        // Headings skipped; ¶ removed; "qu’\+w il" joined.
        QCOMPARE(three[2].text, QStringLiteral("This is the judgment, qu’il came."));
        QCOMPARE(b.chapters.at(4).front().text, QStringLiteral("Chapter four."));
        QCOMPARE(b.verseCount(), 4);
    }

    void usfmWithoutIdIsRejected()
    {
        QVERIFY(!parseUsfmBook(QStringLiteral("\\c 1\n\\v 1 Text\n")).ok());
    }

    void usfx()
    {
        auto books = parseUsfx(R"(<?xml version="1.0"?>
<usfx>
  <book id="PSA">
    <h>Psalms</h><toc level="3">Ps</toc>
    <c id="23"/>
    <d>A Psalm by David.</d>
    <p><v id="1"/>Yahweh is my shepherd:<f>A note</f> I shall lack nothing.<ve/></p>
    <q><v id="2"/>He makes me <w s="H7257">lie down</w> in green pastures.<ve/></q>
    <s>Heading</s>
    <p><v id="3-4"/>Combined verses.<ve/></p>
  </book>
</usfx>)");
        QVERIFY(books.ok());
        QCOMPARE(books.value().size(), std::size_t(1));
        const ParsedBook &psalms = books.value().front();
        QCOMPARE(psalms.code, QStringLiteral("PSA"));
        QCOMPARE(psalms.abbreviation, QStringLiteral("Ps"));
        const auto &verses = psalms.chapters.at(23);
        QCOMPARE(verses.size(), std::size_t(3));
        QCOMPARE(verses[0].text, QStringLiteral("Yahweh is my shepherd: I shall lack nothing."));
        QCOMPARE(verses[1].text, QStringLiteral("He makes me lie down in green pastures."));
        QCOMPARE(verses[2], (Verse{3, 4, "Combined verses."}));
        QVERIFY(!parseUsfx("<html/>").ok());
    }

    // ---- the store ------------------------------------------------------------

    void storePassages()
    {
        auto store = BibleStore::open(QStringLiteral(":memory:"), false);
        QVERIFY(store.ok());
        const ParsedBook john = parseUsfmBook(johnUsfm).value();
        QVERIFY(store.value()->add({"test", "TST", "Test Bible", "Yoruba", "Public domain",
                                    "Test Bible. Public domain.", "", ""},
                                   {john}).ok());

        BookNames names;
        QVERIFY(store.value()->addBookNames(names).ok());
        QCOMPARE(names.find("Johanu"), QStringLiteral("JHN"));
        QCOMPARE(names.find("Jh"), QStringLiteral("JHN"));

        auto passage = store.value()->passage("test", ref("JHN", 3, 16, 3, 17));
        QVERIFY(passage.ok());
        QCOMPARE(passage.value().label, QStringLiteral("Johanu 3:16–17"));
        QCOMPARE(passage.value().translation.copyright, QStringLiteral("Test Bible. Public domain."));
        QCOMPARE(passage.value().verses.size(), std::size_t(2)); // 16 and the combined 17-18

        // A range ending inside a combined verse still includes it.
        QCOMPARE(store.value()->passage("test", ref("JHN", 3, 18, 3, 18)).value().verses.front().verse, 17);

        // Clamped to the chapter; whole chapters; across chapters.
        auto toEnd = store.value()->passage("test", ref("JHN", 3, 16, 3, 999));
        QCOMPARE(toEnd.value().verses.size(), std::size_t(3));
        QCOMPARE(toEnd.value().label, QStringLiteral("Johanu 3:16–19"));
        QCOMPARE(store.value()->passage("test", ref("JHN", 3, 0, 3, 0)).value().verses.size(), std::size_t(3));
        QCOMPARE(store.value()->passage("test", ref("JHN", 3, 19, 4, 1)).value().verses.size(), std::size_t(2));

        // Clear errors for what does not exist.
        auto tooFar = store.value()->passage("test", ref("JHN", 9, 1, 9, 1));
        QVERIFY(!tooFar.ok());
        QCOMPARE(tooFar.error().message, QStringLiteral("Johanu has 4 chapters"));
        auto noVerse = store.value()->passage("test", ref("JHN", 3, 40, 3, 40));
        QCOMPARE(noVerse.error().message, QStringLiteral("Johanu 3 has 19 verses"));
        QCOMPARE(store.value()->passage("test", ref("ROM", 8, 28, 8, 28)).error().message,
                 QStringLiteral("TST does not include Romans"));
        QVERIFY(!store.value()->passage("nope", ref("JHN", 3, 16, 3, 16)).ok());
    }

    void readOnlyStoresRefuseWrites()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("bibles.db");
        {
            auto store = BibleStore::open(path, false);
            QVERIFY(store.ok());
            QVERIFY(store.value()->add({"t", "T", "T", "", "", "", "", ""}, {parseUsfmBook(johnUsfm).value()}).ok());
        }
        auto readOnly = BibleStore::open(path, true);
        QVERIFY(readOnly.ok());
        QVERIFY(!readOnly.value()->add({"u", "U", "U", "", "", "", "", ""}, {}).ok());
        QCOMPARE(readOnly.value()->translations().value().size(), std::size_t(1));
        QVERIFY(!BibleStore::open(dir.filePath("missing.db"), true).ok());
    }

    void readBibleFolders()
    {
        QTemporaryDir dir;
        auto write = [&dir](const QString &name, const QString &text) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("\xEF\xBB\xBF" + text.toUtf8());
        };
        write("44-JHN.usfm", johnUsfm);
        write("01-GEN.SFM", "\\id GEN\n\\c 1\n\\p\n\\v 1 In the beginning.\n");
        write("00-FRT.usfm", "\\id FRT\n\\mt Front matter\n");
        auto books = readBibleFiles(dir.path());
        QVERIFY(books.ok());
        QCOMPARE(books.value().size(), std::size_t(2));
        QCOMPARE(books.value().front().code, QStringLiteral("GEN")); // canonical order
        QCOMPARE(books.value().front().name, QStringLiteral("Genesis")); // English name when none given
        QVERIFY(!readBibleFiles(QTemporaryDir().path()).ok());
    }
};

QTEST_GUILESS_MAIN(TestScripture)
#include "tst_scripture.moc"
