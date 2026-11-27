// Bibles in other programs' formats: Zefania XML, OpenSong XML, OpenLP CSV.

#include "lumen/core/scripture/bible.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QXmlStreamReader>

#include <algorithm>

namespace lumen::core::scripture {

namespace {

// The code of the n-th canonical book (1-based), or empty.
QString codeAt(int ordinal)
{
    const auto &books = canonicalBooks();
    return ordinal >= 1 && ordinal <= int(books.size()) ? QString::fromLatin1(books[std::size_t(ordinal - 1)].code) : QString();
}

// The code for an English book name or abbreviation ("Genesis", "1 Kings",
// "Gen"), or empty.
QString codeForName(const QString &name)
{
    const QString key = name.toLower().remove(u' ').remove(u'.');
    for (const BookInfo &book : canonicalBooks()) {
        if (QString::fromLatin1(book.englishName).toLower().remove(u' ') == key)
            return QString::fromLatin1(book.code);
        for (const char *abbreviation : book.abbreviations) {
            if (key == QLatin1String(abbreviation))
                return QString::fromLatin1(book.code);
        }
    }
    return {};
}

// Verse text as shown: whitespace collapsed, and a verse number some
// converters leave at the start (" 1 Ní atetekọṣe…") removed.
QString cleanVerse(QString text, int number)
{
    text = text.simplified();
    const QString prefix = QString::number(number) + u' ';
    if (text.startsWith(prefix))
        text = text.mid(prefix.size()).trimmed();
    return text;
}

int firstNumber(const QString &text)
{
    static const QRegularExpression digits(QStringLiteral("\\d+"));
    const auto m = digits.match(text);
    return m.hasMatch() ? m.captured().toInt() : 0;
}

void addVerse(ParsedBook &book, int chapter, int verse, const QString &text)
{
    if (chapter <= 0 || verse <= 0 || text.isEmpty())
        return;
    book.chapters[chapter].push_back({verse, verse, text});
}

Result<std::vector<ParsedBook>> finish(std::vector<ParsedBook> books, const QString &format)
{
    books.erase(std::remove_if(books.begin(), books.end(), [](const ParsedBook &b) { return b.code.isEmpty() || b.chapters.empty(); }),
                books.end());
    if (books.empty())
        return Error{Error::Code::Invalid, QStringLiteral("No Bible books found in the %1 file").arg(format)};
    for (ParsedBook &book : books) {
        if (book.name.isEmpty()) {
            if (const BookInfo *info = bookInfo(book.code))
                book.name = QString::fromLatin1(info->englishName);
        }
        for (auto &[chapter, verses] : book.chapters)
            std::sort(verses.begin(), verses.end(), [](const Verse &a, const Verse &b) { return a.number < b.number; });
    }
    std::sort(books.begin(), books.end(), [](const ParsedBook &a, const ParsedBook &b) { return bookOrdinal(a.code) < bookOrdinal(b.code); });
    return books;
}

// The text inside the current element, without notes.
QString elementText(QXmlStreamReader &xml, const QStringList &skipped)
{
    QString text;
    int depth = 1;
    int skipping = 0;
    while (depth > 0 && !xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement:
            ++depth;
            if (skipping || skipped.contains(xml.name().toString(), Qt::CaseInsensitive))
                ++skipping;
            break;
        case QXmlStreamReader::EndElement:
            --depth;
            if (skipping)
                --skipping;
            break;
        case QXmlStreamReader::Characters:
            if (!skipping)
                text += xml.text();
            break;
        default:
            break;
        }
    }
    return text;
}

// One CSV record per call: quoted fields may hold commas, quotes ("") and
// line breaks.
QList<QStringList> parseCsv(const QString &text)
{
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == u'"') {
                if (i + 1 < text.size() && text.at(i + 1) == u'"') {
                    field += u'"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
        } else if (c == u'"') {
            quoted = true;
        } else if (c == u',') {
            row << field;
            field.clear();
        } else if (c == u'\n' || c == u'\r') {
            if (c == u'\r' && i + 1 < text.size() && text.at(i + 1) == u'\n')
                ++i;
            row << field;
            field.clear();
            if (!(row.size() == 1 && row.front().isEmpty()))
                rows << row;
            row.clear();
        } else {
            field += c;
        }
    }
    if (!field.isEmpty() || !row.isEmpty()) {
        row << field;
        rows << row;
    }
    return rows;
}

QString decode(const QByteArray &bytes)
{
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInvalidToNull);
    QString text = utf8(bytes);
    if (utf8.hasError()) // older tools: Windows-1252 / Latin-1
        text = QString::fromLatin1(bytes);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    return text;
}

} // namespace

Result<std::vector<ParsedBook>> parseZefania(const QByteArray &bytes)
{
    QXmlStreamReader xml(bytes);
    if (!xml.readNextStartElement() || xml.name().compare(QLatin1String("XMLBIBLE"), Qt::CaseInsensitive) != 0)
        return Error{Error::Code::Invalid, QStringLiteral("Not a Zefania XML Bible")};
    std::vector<ParsedBook> books;
    ParsedBook *book = nullptr;
    int chapter = 0;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const QString name = xml.name().toString().toUpper();
        const auto attrs = xml.attributes();
        if (name == QLatin1String("BIBLEBOOK")) {
            ParsedBook next;
            next.code = codeAt(attrs.value(QStringLiteral("bnumber")).toInt());
            next.name = attrs.value(QStringLiteral("bname")).toString().trimmed();
            next.abbreviation = attrs.value(QStringLiteral("bsname")).toString().trimmed();
            books.push_back(next);
            book = &books.back();
            chapter = 0;
        } else if (name == QLatin1String("CHAPTER")) {
            chapter = attrs.value(QStringLiteral("cnumber")).toInt();
        } else if (name == QLatin1String("VERS") && book) {
            const int verse = attrs.value(QStringLiteral("vnumber")).toInt();
            addVerse(*book, chapter, verse, cleanVerse(elementText(xml, {QStringLiteral("NOTE"), QStringLiteral("XREF")}), verse));
        }
    }
    if (xml.hasError() && books.empty())
        return Error{Error::Code::Invalid, QStringLiteral("The Zefania file could not be read: %1").arg(xml.errorString())};
    return finish(std::move(books), QStringLiteral("Zefania"));
}

Result<std::vector<ParsedBook>> parseOpenSong(const QByteArray &bytes)
{
    QXmlStreamReader xml(bytes);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("bible"))
        return Error{Error::Code::Invalid, QStringLiteral("Not an OpenSong Bible")};
    std::vector<ParsedBook> books;
    ParsedBook *book = nullptr;
    int chapter = 0;
    int position = 0;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const auto name = xml.name();
        const auto attrs = xml.attributes();
        if (name == QLatin1String("b")) {
            ++position;
            ParsedBook next;
            next.name = attrs.value(QStringLiteral("n")).toString().trimmed();
            // By its English name when it has one, else by its place.
            next.code = codeForName(next.name);
            if (next.code.isEmpty())
                next.code = codeAt(position);
            books.push_back(next);
            book = &books.back();
            chapter = 0;
        } else if (name == QLatin1String("c")) {
            chapter = firstNumber(attrs.value(QStringLiteral("n")).toString());
        } else if (name == QLatin1String("v") && book) {
            const int verse = firstNumber(attrs.value(QStringLiteral("n")).toString());
            addVerse(*book, chapter, verse, cleanVerse(elementText(xml, {}), verse));
        }
    }
    if (xml.hasError() && books.empty())
        return Error{Error::Code::Invalid, QStringLiteral("The OpenSong file could not be read: %1").arg(xml.errorString())};
    return finish(std::move(books), QStringLiteral("OpenSong"));
}

Result<std::vector<ParsedBook>> parseOpenLpCsv(const QByteArray &booksCsv, const QByteArray &versesCsv)
{
    // books.csv: id (1-66), testament, name, abbreviation.
    std::vector<ParsedBook> books;
    QHash<QString, std::size_t> byName;
    for (const QStringList &row : parseCsv(decode(booksCsv))) {
        if (row.size() < 3)
            continue;
        bool numeric = false;
        const int id = row.at(0).trimmed().toInt(&numeric);
        if (!numeric)
            continue; // a header line
        ParsedBook book;
        book.code = codeAt(id);
        book.name = row.at(2).trimmed();
        book.abbreviation = row.size() > 3 ? row.at(3).trimmed() : QString();
        byName.insert(book.name.toLower(), books.size());
        byName.insert(row.at(0).trimmed(), books.size());
        books.push_back(book);
    }
    if (books.empty())
        return Error{Error::Code::Invalid, QStringLiteral("books.csv lists no books")};
    // verses.csv: book (name or id), chapter, verse, text.
    for (const QStringList &row : parseCsv(decode(versesCsv))) {
        if (row.size() < 4)
            continue;
        const QString key = row.at(0).trimmed();
        const auto found = byName.constFind(key.toLower());
        const auto index = found != byName.cend() ? found : byName.constFind(key);
        if (index == byName.cend())
            continue;
        const int verse = row.at(2).trimmed().toInt();
        addVerse(books[index.value()], row.at(1).trimmed().toInt(), verse, cleanVerse(row.mid(3).join(u','), verse));
    }
    return finish(std::move(books), QStringLiteral("OpenLP CSV"));
}

} // namespace lumen::core::scripture
