#include "lumen/core/scripture/biblestore.h"
#include "lumen/core/textfold.h"

#include <optional>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <algorithm>

namespace lumen::core::scripture {

namespace {

Error sqlFailure(const QSqlQuery &query, const QString &context)
{
    return Error{Error::Code::Database, context + QStringLiteral(": ") + query.lastError().text()};
}

Error notFound(const QString &message)
{
    return Error{Error::Code::NotFound, message};
}

// UTF-8 or UTF-16 text (USFM files are UTF-8; a BOM is allowed).
QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    return QString::fromUtf8(bytes);
}

} // namespace

BibleStore::BibleStore(QString connection, bool readOnly)
    : m_connection(std::move(connection)), m_readOnly(readOnly)
{}

BibleStore::~BibleStore()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

Result<std::unique_ptr<BibleStore>> BibleStore::open(const QString &path, bool readOnly)
{
    if (readOnly && !QFileInfo::exists(path))
        return notFound(QStringLiteral("No Bible file at %1").arg(path));
    const QString name = QStringLiteral("lumen-bibles-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::unique_ptr<BibleStore> store(new BibleStore(name, readOnly));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (readOnly)
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open())
            return Error{Error::Code::Database, QStringLiteral("Opening %1: %2").arg(path, db.lastError().text())};
    }
    if (!readOnly) {
        if (auto s = store->createSchema(); !s.ok())
            return s.error();
    }
    return Result<std::unique_ptr<BibleStore>>(std::move(store));
}

Status BibleStore::createSchema()
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    for (const char *sql : {
             "CREATE TABLE IF NOT EXISTS translations (id TEXT PRIMARY KEY, abbreviation TEXT, title TEXT, "
             "language TEXT, licence TEXT, copyright TEXT, source TEXT, notice TEXT, ordinal INTEGER)",
             "CREATE TABLE IF NOT EXISTS books (translation TEXT, code TEXT, ordinal INTEGER, name TEXT, "
             "long_name TEXT, abbreviation TEXT, PRIMARY KEY (translation, code))",
             "CREATE TABLE IF NOT EXISTS verses (translation TEXT, book TEXT, chapter INTEGER, verse INTEGER, "
             "last INTEGER, text TEXT, PRIMARY KEY (translation, book, chapter, verse)) WITHOUT ROWID"}) {
        if (!q.exec(QLatin1String(sql)))
            return sqlFailure(q, QStringLiteral("Creating the Bible tables"));
    }
    return success();
}

Status BibleStore::add(const Translation &t, const std::vector<ParsedBook> &books)
{
    if (m_readOnly)
        return Error{Error::Code::Invalid, QStringLiteral("This Bible file is read-only")};
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    if (!db.transaction())
        return Error{Error::Code::Database, db.lastError().text()};
    auto fail = [&db](const QSqlQuery &q, const QString &context) {
        Error error = sqlFailure(q, context);
        db.rollback();
        return error;
    };

    QSqlQuery q(db);
    for (const char *table : {"verses", "books"}) {
        q.prepare(QStringLiteral("DELETE FROM %1 WHERE translation = ?").arg(QLatin1String(table)));
        q.addBindValue(t.id);
        if (!q.exec())
            return fail(q, QStringLiteral("Replacing translation"));
    }
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO translations VALUES (?, ?, ?, ?, ?, ?, ?, ?, "
                             "COALESCE((SELECT ordinal FROM translations WHERE id = ?), "
                             "(SELECT COUNT(*) FROM translations)))"));
    for (const QString &value : {t.id, t.abbreviation, t.title, t.language, t.licence, t.copyright, t.source, t.notice})
        q.addBindValue(value);
    q.addBindValue(t.id);
    if (!q.exec())
        return fail(q, QStringLiteral("Saving translation"));

    QSqlQuery book(db);
    book.prepare(QStringLiteral("INSERT INTO books VALUES (?, ?, ?, ?, ?, ?)"));
    QSqlQuery verse(db);
    verse.prepare(QStringLiteral("INSERT OR REPLACE INTO verses VALUES (?, ?, ?, ?, ?, ?)"));
    for (const ParsedBook &b : books) {
        book.addBindValue(t.id);
        book.addBindValue(b.code);
        book.addBindValue(bookOrdinal(b.code));
        book.addBindValue(b.name);
        book.addBindValue(b.longName);
        book.addBindValue(b.abbreviation);
        if (!book.exec())
            return fail(book, QStringLiteral("Saving %1").arg(b.code));
        for (const auto &[chapter, verses] : b.chapters) {
            for (const Verse &v : verses) {
                if (v.text.isEmpty())
                    continue;
                verse.addBindValue(t.id);
                verse.addBindValue(b.code);
                verse.addBindValue(chapter);
                verse.addBindValue(v.number);
                verse.addBindValue(v.last);
                verse.addBindValue(v.text);
                if (!verse.exec())
                    return fail(verse, QStringLiteral("Saving %1 %2:%3").arg(b.code).arg(chapter).arg(v.number));
            }
        }
    }
    if (!db.commit())
        return Error{Error::Code::Database, db.lastError().text()};
    return success();
}

Result<std::vector<Translation>> BibleStore::translations() const
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (!q.exec(QStringLiteral("SELECT id, abbreviation, title, language, licence, copyright, source, notice "
                               "FROM translations ORDER BY ordinal, id")))
        return sqlFailure(q, QStringLiteral("Listing translations"));
    std::vector<Translation> out;
    while (q.next()) {
        out.push_back({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                       q.value(3).toString(), q.value(4).toString(), q.value(5).toString(),
                       q.value(6).toString(), q.value(7).toString()});
    }
    return out;
}

Result<std::vector<std::pair<QString, QString>>> BibleStore::books(const QString &translationId) const
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(QStringLiteral("SELECT code, name FROM books WHERE translation = ? ORDER BY ordinal"));
    q.addBindValue(translationId);
    if (!q.exec())
        return sqlFailure(q, QStringLiteral("Listing books"));
    std::vector<std::pair<QString, QString>> out;
    while (q.next())
        out.emplace_back(q.value(0).toString(), q.value(1).toString());
    return out;
}

Status BibleStore::addBookNames(BookNames &names) const
{
    QSqlQuery q(QSqlDatabase::database(m_connection));
    if (!q.exec(QStringLiteral("SELECT code, name, long_name, abbreviation FROM books")))
        return sqlFailure(q, QStringLiteral("Reading book names"));
    while (q.next()) {
        const QString code = q.value(0).toString();
        for (int column = 1; column <= 3; ++column)
            names.add(q.value(column).toString(), code);
    }
    return success();
}

Result<std::vector<VerseHit>> BibleStore::search(const QString &translationId, const QString &query, int limit,
                                                 int *total) const
{
    // A quoted query is one phrase; otherwise every word, anywhere.
    const QString trimmed = query.trimmed();
    const bool phrase = trimmed.size() > 2 && trimmed.startsWith(u'"') && trimmed.endsWith(u'"');
    const QString folded = foldForSearch(phrase ? trimmed.mid(1, trimmed.size() - 2) : trimmed);
    const QStringList words = phrase ? QStringList{folded.simplified()} : folded.split(u' ', Qt::SkipEmptyParts);
    if (total)
        *total = 0;
    if (words.isEmpty() || words.join(QString()).size() < 2)
        return std::vector<VerseHit>{};

    QSqlDatabase db = QSqlDatabase::database(m_connection);
    QHash<QString, QString> names;
    {
        QSqlQuery books(db);
        books.prepare(QStringLiteral("SELECT code, name FROM books WHERE translation = ?"));
        books.addBindValue(translationId);
        if (!books.exec())
            return sqlFailure(books, QStringLiteral("Reading book names"));
        while (books.next())
            names.insert(books.value(0).toString(), books.value(1).toString());
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT book, chapter, verse, text FROM verses WHERE translation = ?"));
    q.addBindValue(translationId);
    if (!q.exec())
        return sqlFailure(q, QStringLiteral("Searching the Bible"));
    std::vector<VerseHit> hits;
    while (q.next()) {
        const QString text = q.value(3).toString();
        const QString haystack = u' ' + foldForSearch(text) + u' ';
        bool all = true;
        for (const QString &word : words) {
            if (!haystack.contains(word)) {
                all = false;
                break;
            }
        }
        if (all) {
            const QString book = q.value(0).toString();
            hits.push_back({book, names.value(book, book), q.value(1).toInt(), q.value(2).toInt(), text});
        }
    }
    std::sort(hits.begin(), hits.end(), [](const VerseHit &a, const VerseHit &b) {
        const int oa = bookOrdinal(a.book), ob = bookOrdinal(b.book);
        return oa != ob ? oa < ob : a.chapter != b.chapter ? a.chapter < b.chapter : a.verse < b.verse;
    });
    if (total)
        *total = int(hits.size());
    if (int(hits.size()) > limit)
        hits.resize(std::size_t(limit));
    return hits;
}

Result<Passage> BibleStore::passage(const QString &translationId, const Reference &reference) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    Passage passage;

    auto list = translations();
    if (!list.ok())
        return list.error();
    const auto found = std::find_if(list.value().begin(), list.value().end(),
                                    [&](const Translation &t) { return t.id == translationId; });
    if (found == list.value().end())
        return notFound(QStringLiteral("No translation \"%1\"").arg(translationId));
    passage.translation = *found;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT name FROM books WHERE translation = ? AND code = ?"));
    q.addBindValue(translationId);
    q.addBindValue(reference.book);
    if (!q.exec())
        return sqlFailure(q, QStringLiteral("Reading book"));
    const BookInfo *info = bookInfo(reference.book);
    const QString english = info ? QLatin1String(info->englishName) : reference.book;
    if (!q.next())
        return notFound(QStringLiteral("%1 does not include %2").arg(passage.translation.abbreviation, english));
    passage.bookName = q.value(0).toString().isEmpty() ? english : q.value(0).toString();

    // The real shape of the chapters asked for.
    auto lastVerse = [&](int chapter) -> int {
        QSqlQuery m(db);
        m.prepare(QStringLiteral("SELECT MAX(last) FROM verses WHERE translation = ? AND book = ? AND chapter = ?"));
        m.addBindValue(translationId);
        m.addBindValue(reference.book);
        m.addBindValue(chapter);
        return m.exec() && m.next() ? m.value(0).toInt() : 0;
    };
    QSqlQuery c(db);
    c.prepare(QStringLiteral("SELECT MAX(chapter) FROM verses WHERE translation = ? AND book = ?"));
    c.addBindValue(translationId);
    c.addBindValue(reference.book);
    const int chapters = c.exec() && c.next() ? c.value(0).toInt() : 0;
    if (reference.chapter > chapters) {
        return notFound(chapters == 1 ? QStringLiteral("%1 has 1 chapter").arg(passage.bookName)
                                      : QStringLiteral("%1 has %2 chapters").arg(passage.bookName).arg(chapters));
    }

    Reference ref = reference;
    ref.endChapter = qMin(qMax(ref.endChapter, ref.chapter), chapters);
    const int firstChapterLength = lastVerse(ref.chapter);
    if (!ref.wholeChapters() && ref.verse > firstChapterLength) {
        return notFound(QStringLiteral("%1 %2 has %3 verses").arg(passage.bookName).arg(ref.chapter).arg(firstChapterLength));
    }
    const int fromVerse = ref.wholeChapters() ? 1 : ref.verse;
    const int toVerse = ref.wholeChapters() ? lastVerse(ref.endChapter) : qMin(ref.endVerse, lastVerse(ref.endChapter));
    if (!ref.wholeChapters())
        ref.endVerse = toVerse;
    passage.reference = ref;
    passage.label = formatReference(ref, passage.bookName);

    QSqlQuery v(db);
    v.prepare(QStringLiteral("SELECT chapter, verse, last, text FROM verses WHERE translation = ? AND book = ? "
                             "AND (chapter > ? OR (chapter = ? AND last >= ?)) "
                             "AND (chapter < ? OR (chapter = ? AND verse <= ?)) ORDER BY chapter, verse"));
    v.addBindValue(translationId);
    v.addBindValue(reference.book);
    v.addBindValue(ref.chapter);
    v.addBindValue(ref.chapter);
    v.addBindValue(fromVerse);
    v.addBindValue(ref.endChapter);
    v.addBindValue(ref.endChapter);
    v.addBindValue(toVerse);
    if (!v.exec())
        return sqlFailure(v, QStringLiteral("Reading verses"));
    while (v.next()) {
        passage.verses.push_back({reference.book, v.value(0).toInt(), v.value(1).toInt(), v.value(2).toInt(),
                                  v.value(3).toString()});
    }
    if (passage.verses.empty())
        return notFound(QStringLiteral("%1 has no text for %2").arg(passage.translation.abbreviation, passage.label));
    return passage;
}

namespace {

// A single-file Bible by its contents (OpenSong files often end in .xmm or
// nothing at all): USFX, Zefania or OpenSong XML.
std::optional<Result<std::vector<ParsedBook>>> readXmlBible(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray bytes = file.readAll();
    const QByteArray head = bytes.left(4096);
    if (head.contains("<usfx"))
        return parseUsfx(bytes);
    if (head.contains("<XMLBIBLE") || head.contains("<xmlbible"))
        return parseZefania(bytes);
    if (head.contains("<bible"))
        return parseOpenSong(bytes);
    return std::nullopt;
}

// OpenLP CSV: books.csv and verses.csv in one folder.
std::optional<Result<std::vector<ParsedBook>>> readCsvBible(const QDir &folder)
{
    QFile books(folder.filePath(QStringLiteral("books.csv")));
    QFile verses(folder.filePath(QStringLiteral("verses.csv")));
    if (!books.exists() || !verses.exists())
        return std::nullopt;
    if (!books.open(QIODevice::ReadOnly) || !verses.open(QIODevice::ReadOnly))
        return Result<std::vector<ParsedBook>>(Error{Error::Code::Invalid, QStringLiteral("books.csv or verses.csv cannot be read")});
    return parseOpenLpCsv(books.readAll(), verses.readAll());
}

} // namespace

Result<std::vector<ParsedBook>> readBibleFiles(const QString &folderOrFile)
{
    const QFileInfo info(folderOrFile);
    if (info.isFile() && info.suffix().compare(QLatin1String("csv"), Qt::CaseInsensitive) == 0) {
        if (auto csv = readCsvBible(info.dir()))
            return *csv;
        return Error{Error::Code::Invalid, QStringLiteral("An OpenLP CSV Bible needs books.csv and verses.csv side by side")};
    }
    if (info.isFile() && !folderOrFile.endsWith(QLatin1String(".usfm"), Qt::CaseInsensitive)
        && !folderOrFile.endsWith(QLatin1String(".sfm"), Qt::CaseInsensitive)) {
        if (auto xml = readXmlBible(folderOrFile))
            return *xml;
    }
    if (info.isDir()) {
        if (auto csv = readCsvBible(QDir(folderOrFile)))
            return *csv;
    }

    QStringList files;
    if (info.isDir()) {
        for (const QFileInfo &entry : QDir(folderOrFile).entryInfoList(
                 {QStringLiteral("*.usfm"), QStringLiteral("*.sfm"), QStringLiteral("*.SFM")}, QDir::Files, QDir::Name))
            files << entry.filePath();
    } else if (info.isFile()) {
        files << folderOrFile;
    }
    if (files.isEmpty() && info.isDir()) {
        // A folder holding one XML Bible file instead (USFX, Zefania,
        // OpenSong).
        for (const QFileInfo &candidate : QDir(folderOrFile).entryInfoList(QDir::Files, QDir::Name)) {
            if (auto books = readXmlBible(candidate.filePath()); books && books->ok())
                return *books;
        }
    }
    if (files.isEmpty())
        return Error{Error::Code::Invalid, QStringLiteral("No Bible found. Lumen reads USFM, USFX, Zefania and OpenSong XML, and OpenLP CSV (books.csv and verses.csv)")};

    std::vector<ParsedBook> books;
    for (const QString &path : files) {
        auto book = parseUsfmBook(readText(path));
        if (!book.ok())
            continue; // front matter, glossaries and the like
        if (book.value().code == QLatin1String("FRT") || book.value().code == QLatin1String("BAK")
            || book.value().code == QLatin1String("GLO") || book.value().code == QLatin1String("CNC")
            || book.value().code == QLatin1String("INT") || book.value().code == QLatin1String("OTH")
            || book.value().chapters.empty())
            continue;
        books.push_back(std::move(book).value());
    }
    if (books.empty())
        return Error{Error::Code::Invalid, QStringLiteral("No Bible books found in %1").arg(folderOrFile)};
    std::sort(books.begin(), books.end(),
              [](const ParsedBook &a, const ParsedBook &b) { return bookOrdinal(a.code) < bookOrdinal(b.code); });
    return books;
}

} // namespace lumen::core::scripture
