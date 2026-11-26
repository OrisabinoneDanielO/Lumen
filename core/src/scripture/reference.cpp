#include "lumen/core/scripture/reference.h"

#include "lumen/core/scripture/bible.h"
#include "lumen/core/textfold.h"

#include <QRegularExpression>
#include <QSet>

namespace lumen::core::scripture {

namespace {

Error invalid(const QString &message)
{
    return Error{Error::Code::Invalid, message};
}

} // namespace

QString BookNames::key(const QString &text)
{
    QString folded = foldForSearch(text);
    folded.remove(u' ');
    return folded;
}

BookNames::BookNames()
{
    for (const BookInfo &book : canonicalBooks()) {
        const QString code = QLatin1String(book.code);
        addBuiltIn(key(QLatin1String(book.englishName)), code);
        m_names.insert(key(QLatin1String(book.englishName)), code);
        addBuiltIn(key(code), code);
        for (const char *abbreviation : book.abbreviations)
            addBuiltIn(QLatin1String(abbreviation), code);
    }
    // Common alternatives.
    addBuiltIn(QStringLiteral("psalm"), QStringLiteral("PSA"));
    addBuiltIn(QStringLiteral("songofsolomon"), QStringLiteral("SNG"));
    addBuiltIn(QStringLiteral("revelationofjohn"), QStringLiteral("REV"));
}

void BookNames::addBuiltIn(const QString &k, const QString &code)
{
    m_exact.insert(k, code);
    m_builtIn.insert(k);
}

namespace {

// Case-folded with spaces and dots removed, accents kept.
QString accentedKey(const QString &text)
{
    QString k = text.normalized(QString::NormalizationForm_C).toCaseFolded();
    k.remove(u' ').remove(u'.');
    return k;
}

} // namespace

void BookNames::add(const QString &name, const QString &code)
{
    // With accents: only meaningful when the name has some ("Jọn").
    const QString accented = accentedKey(name);
    if (!accented.isEmpty() && accented != key(name)) {
        if (const auto it = m_accented.constFind(accented); it != m_accented.constEnd() && it.value() != code)
            m_accentedAmbiguous.insert(accented);
        else
            m_accented.insert(accented, code);
    }

    const QString k = key(name);
    if (k.isEmpty() || m_builtIn.contains(k))
        return;
    if (const auto it = m_exact.constFind(k); it != m_exact.constEnd() && it.value() != code) {
        m_ambiguous.insert(k);
        m_names.remove(k);
        return;
    }
    if (m_ambiguous.contains(k))
        return;
    m_exact.insert(k, code);
    m_names.insert(k, code);
}

QString BookNames::find(const QString &typed) const
{
    const QString accented = accentedKey(typed);
    if (const auto it = m_accented.constFind(accented);
        it != m_accented.constEnd() && !m_accentedAmbiguous.contains(accented))
        return it.value();

    const QString k = key(typed);
    if (k.isEmpty())
        return {};
    if (const auto it = m_exact.constFind(k); it != m_exact.constEnd() && !m_ambiguous.contains(k))
        return it.value();
    if (k.size() < 2)
        return {};
    QSet<QString> codes;
    for (auto it = m_names.constBegin(); it != m_names.constEnd(); ++it) {
        if (it.key().startsWith(k))
            codes.insert(it.value());
    }
    return codes.size() == 1 ? *codes.begin() : QString();
}

Result<Reference> parseReference(const QString &input, const BookNames &names)
{
    QString text = input.trimmed();
    for (const char16_t dash : {u'–', u'—', u'−', u'‒'})
        text.replace(QChar(dash), u'-');
    if (text.isEmpty())
        return invalid(QStringLiteral("Type a reference such as John 3:16"));

    // "1 Cor", "I Cor", "First Corinthians", "2nd Kings": a leading number
    // belongs to the book name.
    static const QRegularExpression ordinal(
        QStringLiteral(R"(^(1st|2nd|3rd|first|second|third|iii|ii|i|1|2|3)\.?\s*(?=[^\d\s:.,;-]))"),
        QRegularExpression::CaseInsensitiveOption);
    QString prefix;
    if (const auto match = ordinal.match(text); match.hasMatch()) {
        const QString n = match.captured(1).toLower();
        prefix = (n == QLatin1String("1") || n == QLatin1String("i") || n == QLatin1String("first")
                  || n == QLatin1String("1st"))
                     ? QStringLiteral("1")
                 : (n == QLatin1String("2") || n == QLatin1String("ii") || n == QLatin1String("second")
                    || n == QLatin1String("2nd"))
                     ? QStringLiteral("2")
                     : QStringLiteral("3");
        text = text.mid(match.capturedEnd());
    }

    // The book name runs up to the first digit.
    static const QRegularExpression split(QStringLiteral(R"(^([^\d]+?)\s*(\d.*)?$)"));
    const auto parts = split.match(text);
    if (!parts.hasMatch())
        return invalid(QStringLiteral("Start with a book, e.g. John 3:16"));
    QString bookText = parts.captured(1).trimmed();
    while (bookText.endsWith(u'.'))
        bookText.chop(1);
    const QString book = names.find(prefix + bookText);
    if (book.isEmpty())
        return invalid(QStringLiteral("Which book is \"%1\"?").arg((prefix + u' ' + bookText).trimmed()));
    const BookInfo *info = bookInfo(book);
    const bool singleChapter = info && info->singleChapter;

    // Numbers: separators ":" "." "," and spaces; "-" for ranges.
    QString numbers = parts.captured(2);
    numbers.replace(u':', u' ').replace(u'.', u' ').replace(u',', u' ').replace(u'-', QStringLiteral(" - "));
    const QStringList tokens = numbers.split(u' ', Qt::SkipEmptyParts);
    QList<int> n;
    QList<qsizetype> dashes;
    for (const QString &token : tokens) {
        if (token == QLatin1String("-")) {
            dashes << n.size();
            continue;
        }
        bool ok = false;
        const int value = token.toInt(&ok);
        if (!ok || value <= 0)
            return invalid(QStringLiteral("\"%1\" is not a chapter or verse number").arg(token));
        n << value;
    }
    if (dashes.size() > 1)
        return invalid(QStringLiteral("Use one range, e.g. John 3:16-18"));
    const bool range = !dashes.isEmpty();
    const qsizetype dashAt = range ? dashes.front() : -1;

    Reference ref;
    ref.book = book;
    if (n.isEmpty()) {
        if (!singleChapter)
            return invalid(QStringLiteral("Add a chapter, e.g. %1 1").arg(info ? QLatin1String(info->englishName) : book));
        ref.chapter = ref.endChapter = 1; // "Jude": the whole book
        return ref;
    }

    // Books with one chapter take verse numbers directly: "Jude 3", "Jude 3-5",
    // "Jude 3-". "Jude 1:3" also works (below).
    if (singleChapter) {
        const bool verseOnly = (!range && n.size() == 1) || (range && dashAt == 1 && n.size() <= 2);
        if (verseOnly) {
            ref.chapter = ref.endChapter = 1;
            ref.verse = n[0];
            ref.endVerse = !range ? n[0] : n.size() == 2 ? n[1] : 999;
            if (ref.endVerse < ref.verse)
                return invalid(QStringLiteral("The range ends before it starts"));
            return ref;
        }
    }

    if (!range) {
        switch (n.size()) {
        case 1: // "John 3"
            ref.chapter = ref.endChapter = n[0];
            return ref;
        case 2: // "John 3 16"
            ref.chapter = ref.endChapter = n[0];
            ref.verse = ref.endVerse = n[1];
            return ref;
        case 3: // "John 3 16 18": a range without the dash
            ref.chapter = ref.endChapter = n[0];
            ref.verse = n[1];
            ref.endVerse = n[2];
            break;
        default:
            return invalid(QStringLiteral("Too many numbers"));
        }
    } else if (n.size() == 2 && dashAt == 1) { // "John 3-4": chapters
        ref.chapter = n[0];
        ref.endChapter = n[1];
    } else if (n.size() == 3 && dashAt == 2) { // "John 3:16-18"
        ref.chapter = ref.endChapter = n[0];
        ref.verse = n[1];
        ref.endVerse = n[2];
    } else if (n.size() == 4 && dashAt == 2) { // "Genesis 1:1-2:3"
        ref.chapter = n[0];
        ref.verse = n[1];
        ref.endChapter = n[2];
        ref.endVerse = n[3];
    } else if (n.size() == 2 && dashAt == 2) { // "John 3:16-": to the end of the chapter
        ref.chapter = ref.endChapter = n[0];
        ref.verse = n[1];
        ref.endVerse = 999;
    } else {
        return invalid(QStringLiteral("Chapter and verse not understood; try John 3:16-18"));
    }

    if (ref.endChapter < ref.chapter || (ref.endChapter == ref.chapter && ref.endVerse < ref.verse))
        return invalid(QStringLiteral("The range ends before it starts"));
    return ref;
}

QString formatReference(const Reference &ref, const QString &bookName)
{
    const QString dash = QStringLiteral("–");
    if (ref.wholeChapters()) {
        if (ref.endChapter > ref.chapter)
            return QStringLiteral("%1 %2%3%4").arg(bookName).arg(ref.chapter).arg(dash).arg(ref.endChapter);
        return QStringLiteral("%1 %2").arg(bookName).arg(ref.chapter);
    }
    QString out = QStringLiteral("%1 %2:%3").arg(bookName).arg(ref.chapter).arg(ref.verse);
    if (ref.endChapter != ref.chapter)
        out += dash + QStringLiteral("%1:%2").arg(ref.endChapter).arg(ref.endVerse);
    else if (ref.endVerse != ref.verse)
        out += dash + QString::number(ref.endVerse);
    return out;
}

} // namespace lumen::core::scripture
