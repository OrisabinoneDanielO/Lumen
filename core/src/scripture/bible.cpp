#include "lumen/core/scripture/bible.h"

#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

namespace lumen::core::scripture {

const std::vector<BookInfo> &canonicalBooks()
{
    static const std::vector<BookInfo> books{
        {"GEN", "Genesis", {"gen", "ge", "gn"}},
        {"EXO", "Exodus", {"exo", "ex", "exod"}},
        {"LEV", "Leviticus", {"lev", "le", "lv"}},
        {"NUM", "Numbers", {"num", "nu", "nm", "nb"}},
        {"DEU", "Deuteronomy", {"deut", "deu", "dt", "de"}},
        {"JOS", "Joshua", {"josh", "jos", "jsh"}},
        {"JDG", "Judges", {"judg", "jdg", "jg", "jdgs"}},
        {"RUT", "Ruth", {"ruth", "rut", "rth", "ru"}},
        {"1SA", "1 Samuel", {"1sam", "1sa", "1sm", "1s"}},
        {"2SA", "2 Samuel", {"2sam", "2sa", "2sm", "2s"}},
        {"1KI", "1 Kings", {"1kgs", "1ki", "1kin", "1k"}},
        {"2KI", "2 Kings", {"2kgs", "2ki", "2kin", "2k"}},
        {"1CH", "1 Chronicles", {"1chr", "1ch", "1chron"}},
        {"2CH", "2 Chronicles", {"2chr", "2ch", "2chron"}},
        {"EZR", "Ezra", {"ezr", "ez"}},
        {"NEH", "Nehemiah", {"neh", "ne"}},
        {"EST", "Esther", {"esth", "est", "es"}},
        {"JOB", "Job", {"jb"}},
        {"PSA", "Psalms", {"ps", "psa", "psalm", "pss", "psm"}},
        {"PRO", "Proverbs", {"prov", "pro", "prv", "pr"}},
        {"ECC", "Ecclesiastes", {"eccl", "ecc", "ec", "qoh"}},
        {"SNG", "Song of Songs", {"song", "sos", "sng", "songofsolomon", "canticles"}},
        {"ISA", "Isaiah", {"isa", "is"}},
        {"JER", "Jeremiah", {"jer", "je", "jr"}},
        {"LAM", "Lamentations", {"lam", "la"}},
        {"EZK", "Ezekiel", {"ezek", "eze", "ezk"}},
        {"DAN", "Daniel", {"dan", "da", "dn"}},
        {"HOS", "Hosea", {"hos", "ho"}},
        {"JOL", "Joel", {"joel", "jl"}},
        {"AMO", "Amos", {"amos", "am"}},
        {"OBA", "Obadiah", {"obad", "ob", "oba"}, true},
        {"JON", "Jonah", {"jonah", "jon", "jnh"}},
        {"MIC", "Micah", {"mic", "mc"}},
        {"NAM", "Nahum", {"nah", "na"}},
        {"HAB", "Habakkuk", {"hab", "hb"}},
        {"ZEP", "Zephaniah", {"zeph", "zep", "zp"}},
        {"HAG", "Haggai", {"hag", "hg"}},
        {"ZEC", "Zechariah", {"zech", "zec", "zc"}},
        {"MAL", "Malachi", {"mal", "ml"}},
        {"MAT", "Matthew", {"matt", "mat", "mt"}},
        {"MRK", "Mark", {"mark", "mrk", "mk", "mr"}},
        {"LUK", "Luke", {"luke", "luk", "lk"}},
        {"JHN", "John", {"john", "jhn", "jn", "jo"}},
        {"ACT", "Acts", {"acts", "act", "ac"}},
        {"ROM", "Romans", {"rom", "ro", "rm"}},
        {"1CO", "1 Corinthians", {"1cor", "1co"}},
        {"2CO", "2 Corinthians", {"2cor", "2co"}},
        {"GAL", "Galatians", {"gal", "ga"}},
        {"EPH", "Ephesians", {"eph", "ephes"}},
        {"PHP", "Philippians", {"phil", "php", "pp"}},
        {"COL", "Colossians", {"col"}},
        {"1TH", "1 Thessalonians", {"1thess", "1th", "1thes"}},
        {"2TH", "2 Thessalonians", {"2thess", "2th", "2thes"}},
        {"1TI", "1 Timothy", {"1tim", "1ti", "1tm"}},
        {"2TI", "2 Timothy", {"2tim", "2ti", "2tm"}},
        {"TIT", "Titus", {"titus", "tit", "ti"}},
        {"PHM", "Philemon", {"philem", "phm", "phlm"}, true},
        {"HEB", "Hebrews", {"heb"}},
        {"JAS", "James", {"jas", "jm", "jam"}},
        {"1PE", "1 Peter", {"1pet", "1pe", "1pt", "1p"}},
        {"2PE", "2 Peter", {"2pet", "2pe", "2pt", "2p"}},
        {"1JN", "1 John", {"1john", "1jn", "1jo", "1jhn"}},
        {"2JN", "2 John", {"2john", "2jn", "2jo", "2jhn"}, true},
        {"3JN", "3 John", {"3john", "3jn", "3jo", "3jhn"}, true},
        {"JUD", "Jude", {"jude", "jud", "jd"}, true},
        {"REV", "Revelation", {"rev", "re", "rv", "revelations"}},
    };
    return books;
}

int bookOrdinal(const QString &code)
{
    const auto &books = canonicalBooks();
    for (std::size_t i = 0; i < books.size(); ++i) {
        if (code == QLatin1String(books[i].code))
            return int(i) + 1;
    }
    return 100;
}

const BookInfo *bookInfo(const QString &code)
{
    for (const BookInfo &book : canonicalBooks()) {
        if (code == QLatin1String(book.code))
            return &book;
    }
    return nullptr;
}

int ParsedBook::verseCount() const
{
    int count = 0;
    for (const auto &[chapter, verses] : chapters)
        count += int(verses.size());
    return count;
}

namespace {

Error invalid(const QString &message)
{
    return Error{Error::Code::Invalid, message};
}

// Paragraph and poetry markers: verse text carries on after them.
bool isParagraphMarker(const QString &m)
{
    static const QSet<QString> markers{
        "p", "m", "po", "pr", "cls", "pmo", "pm", "pmc", "pmr", "pi", "pi1", "pi2", "pi3",
        "mi", "nb", "pc", "ph", "ph1", "ph2", "ph3", "q", "q1", "q2", "q3", "q4", "qr",
        "qc", "qa", "qm", "qm1", "qm2", "qm3", "qd", "b", "li", "li1", "li2", "li3", "li4",
        "lh", "lf", "lim", "lim1", "lim2", "tr", "tc1", "tc2", "tc3", "tc4", "th1", "th2"};
    return markers.contains(m);
}

// Markers whose text is not verse text (until the next marker).
bool isSkippedBlock(const QString &m)
{
    static const QRegularExpression skipped(QStringLiteral(
        R"(^(id|ide|h|h\d|toc\d|toca\d|rem|sts|usfm|mt\d?|mte\d?|ms\d?|mr|s\d?|sr|r|d|sp|sd\d?|)"
        R"(cl|cd|cp|ca|va|vp|imt\d?|is\d?|ip|ipi|im|imi|ipq|imq|ipr|iq\d?|ib|ili\d?|iot|io\d?|)"
        R"(iex|imte\d?|ie|restore|periph)$)"));
    Q_ASSERT(skipped.isValid());
    return skipped.match(m).hasMatch();
}

QString tidy(QString text)
{
    text.remove(QChar(0x00B6)); // ¶ paragraph signs some editions keep in the text
    return text.simplified();
}

} // namespace

Result<ParsedBook> parseUsfmBook(const QString &usfm)
{
    ParsedBook book;

    // Footnotes, cross-references and figures go entirely, with their text.
    QString text = usfm;
    static const QRegularExpression notes(QStringLiteral(R"(\\(f|fe|ef|x|ex|fig)\s.*?\\\1\*)"),
                                          QRegularExpression::DotMatchesEverythingOption);
    text.remove(notes);

    // Book identity and names from the header lines.
    static const QRegularExpression header(QStringLiteral(R"(^\\(id|h|toc1|toc2|toc3)\s+(.*)$)"),
                                           QRegularExpression::MultilineOption);
    for (auto it = header.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        const QString marker = match.captured(1);
        const QString value = match.captured(2).trimmed();
        if (marker == QLatin1String("id"))
            book.code = value.section(u' ', 0, 0).toUpper();
        else if (marker == QLatin1String("h") && book.name.isEmpty())
            book.name = value;
        else if (marker == QLatin1String("toc1"))
            book.longName = value;
        else if (marker == QLatin1String("toc2"))
            book.name = value;
        else if (marker == QLatin1String("toc3"))
            book.abbreviation = value;
    }
    if (book.code.isEmpty())
        return invalid(QStringLiteral("No \\id line: not a USFM book"));
    if (const BookInfo *info = bookInfo(book.code); info && book.name.isEmpty())
        book.name = QLatin1String(info->englishName);

    // Walk the markers. Text between markers belongs to the current verse
    // unless a heading-like marker is open.
    static const QRegularExpression marker(QStringLiteral(R"(\\(\+?)([A-Za-z]+\d*)(\*?))"));
    static const QRegularExpression verseNumber(QStringLiteral(R"(^\s*(\d+)[a-z]?(?:\s*-\s*(\d+)[a-z]?)?\s?)"));
    static const QRegularExpression chapterNumber(QStringLiteral(R"(^\s*(\d+))"));

    int chapter = 0;
    Verse *current = nullptr;
    bool skipping = true; // before the first verse everything is heading text
    qsizetype pos = 0;

    auto addText = [&](QString segment) {
        if (skipping || !current)
            return;
        // Word-level attributes: "grace|strong="G5485"" keeps "grace".
        const qsizetype bar = segment.indexOf(u'|');
        if (bar >= 0)
            segment.truncate(bar);
        current->text += segment;
    };

    auto it = marker.globalMatch(text);
    while (true) {
        const bool more = it.hasNext();
        const auto match = more ? it.next() : QRegularExpressionMatch();
        const qsizetype start = more ? match.capturedStart() : text.size();
        addText(text.mid(pos, start - pos));
        if (!more)
            break;
        pos = match.capturedEnd();

        const QString name = match.captured(2);
        const bool closing = !match.captured(3).isEmpty();
        if (closing)
            continue; // the end of a character style: its text was kept
        // One space after an opening marker belongs to the marker, not the
        // text ("qu’\\+w il" is "qu’il").
        if (pos < text.size() && text.at(pos).isSpace())
            ++pos;

        if (name == QLatin1String("c")) {
            const auto number = chapterNumber.match(text.mid(pos, 12));
            if (number.hasMatch()) {
                chapter = number.captured(1).toInt();
                pos += number.capturedEnd();
                book.chapters[chapter];
            }
            current = nullptr;
            skipping = true;
        } else if (name == QLatin1String("v")) {
            const auto number = verseNumber.match(text.mid(pos, 24));
            if (!number.hasMatch() || chapter == 0)
                continue;
            const int first = number.captured(1).toInt();
            const int last = number.captured(2).isEmpty() ? first : number.captured(2).toInt();
            pos += number.capturedEnd();
            auto &verses = book.chapters[chapter];
            verses.push_back({first, qMax(first, last), {}});
            current = &verses.back();
            skipping = false;
        } else if (isParagraphMarker(name)) {
            if (current)
                skipping = false;
            addText(QStringLiteral(" "));
        } else if (isSkippedBlock(name)) {
            skipping = true;
        }
        // Other markers (wj, add, nd, w, qs, sc, ...) are character styles:
        // their text stays.
    }

    for (auto &[number, verses] : book.chapters) {
        for (Verse &verse : verses)
            verse.text = tidy(verse.text);
    }
    return book;
}

Result<std::vector<ParsedBook>> parseUsfx(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    std::vector<ParsedBook> books;
    ParsedBook *book = nullptr;
    Verse *verse = nullptr;
    int chapter = 0;
    int skipDepth = 0; // inside notes, headings and titles
    static const QSet<QString> skipped{"f", "x", "fe", "s", "d", "h", "toc", "rem", "ide", "id",
                                       "fig", "ms", "mt", "sp", "cl", "cp", "ca", "va", "vp", "b"};

    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement: {
            const QString name = reader.name().toString();
            const auto attrs = reader.attributes();
            if (skipDepth > 0) {
                ++skipDepth;
            } else if (name == QLatin1String("book")) {
                books.push_back({});
                book = &books.back();
                book->code = attrs.value(QStringLiteral("id")).toString().toUpper();
                chapter = 0;
                verse = nullptr;
            } else if (book && name == QLatin1String("toc")) {
                const QString level = attrs.value(QStringLiteral("level")).toString();
                const QString value = reader.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
                if (level == QLatin1String("1"))
                    book->longName = value;
                else if (level == QLatin1String("2"))
                    book->name = value;
                else if (level == QLatin1String("3"))
                    book->abbreviation = value;
            } else if (book && name == QLatin1String("h")) {
                const QString value = reader.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
                if (book->name.isEmpty())
                    book->name = value;
            } else if (book && name == QLatin1String("c")) {
                chapter = attrs.value(QStringLiteral("id")).toInt();
                book->chapters[chapter];
                verse = nullptr;
            } else if (book && name == QLatin1String("v") && chapter > 0) {
                const QString id = attrs.value(QStringLiteral("id")).toString();
                const int first = id.section(u'-', 0, 0).toInt();
                const int last = id.contains(u'-') ? id.section(u'-', 1, 1).toInt() : first;
                auto &verses = book->chapters[chapter];
                verses.push_back({first, qMax(first, last), {}});
                verse = &verses.back();
            } else if (name == QLatin1String("ve")) {
                verse = nullptr;
            } else if (skipped.contains(name)) {
                skipDepth = 1;
            }
            break;
        }
        case QXmlStreamReader::EndElement:
            if (skipDepth > 0)
                --skipDepth;
            break;
        case QXmlStreamReader::Characters:
            if (skipDepth == 0 && verse)
                verse->text += reader.text();
            break;
        default:
            break;
        }
    }
    if (reader.hasError())
        return invalid(QStringLiteral("Line %1: %2").arg(reader.lineNumber()).arg(reader.errorString()));
    if (books.empty())
        return invalid(QStringLiteral("No books found: not a USFX file"));

    for (ParsedBook &b : books) {
        if (b.name.isEmpty()) {
            if (const BookInfo *info = bookInfo(b.code))
                b.name = QLatin1String(info->englishName);
        }
        for (auto &[number, verses] : b.chapters) {
            for (Verse &v : verses)
                v.text = tidy(v.text);
        }
    }
    return books;
}

} // namespace lumen::core::scripture
