#include "lumen/core/songs/foreignimport.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QXmlStreamReader>

#include <functional>

namespace lumen::core::songs {

namespace {

Error invalid(const QString &message)
{
    return Error{Error::Code::Invalid, message};
}

// A read-only connection to another program's SQLite file, closed when done.
class ForeignDb
{
public:
    explicit ForeignDb(const QString &path)
        : m_name(QStringLiteral("lumen-import-") + QUuid::createUuid().toString(QUuid::Id128))
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        m_open = QFileInfo(path).isFile() && db.open();
    }
    ~ForeignDb()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_name, false);
            db.close();
        }
        QSqlDatabase::removeDatabase(m_name);
    }
    bool isOpen() const { return m_open; }
    QSqlDatabase db() const { return QSqlDatabase::database(m_name, false); }
    QStringList tables() const { return db().tables(); }

private:
    QString m_name;
    bool m_open = false;
};

// Section labels unique within a song ("Chorus", "Chorus 2", ...).
QString uniqueLabel(const QString &label, const ParsedSong &song)
{
    const auto taken = [&](const QString &l) {
        return std::any_of(song.sections.begin(), song.sections.end(), [&](const ParsedSection &s) { return s.label == l; });
    };
    if (!taken(label))
        return label;
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 %2").arg(label).arg(n);
        if (!taken(candidate))
            return candidate;
    }
}

// Lines as one block of a section, skipping blank ones.
std::vector<LyricLine> linesOf(const QString &text)
{
    std::vector<LyricLine> lines;
    for (const QString &line : QString(text).replace(QStringLiteral("\r\n"), QStringLiteral("\n")).split(u'\n')) {
        if (!line.trimmed().isEmpty())
            lines.push_back({line.trimmed(), {}});
    }
    return lines;
}

// ---- OpenLP ----------------------------------------------------------------

QString openLpLabel(const QString &type, const QString &number, int countOfType)
{
    const QString t = type.toLower();
    const QString n = number.isEmpty() ? QStringLiteral("1") : number;
    const auto numbered = [&](const QString &base) {
        // A lone chorus or bridge needs no number; verses always have one.
        return countOfType <= 1 && n == QLatin1String("1") ? base : base + u' ' + n;
    };
    if (t == QLatin1String("v"))
        return QStringLiteral("Verse ") + n;
    if (t == QLatin1String("c"))
        return numbered(QStringLiteral("Chorus"));
    if (t == QLatin1String("b"))
        return numbered(QStringLiteral("Bridge"));
    if (t == QLatin1String("p"))
        return numbered(QStringLiteral("Pre-chorus"));
    if (t == QLatin1String("i"))
        return numbered(QStringLiteral("Intro"));
    if (t == QLatin1String("e"))
        return numbered(QStringLiteral("Ending"));
    return QStringLiteral("Other ") + n;
}

struct OpenLpVerse
{
    QString type;
    QString number;
    QString text;
};

std::vector<OpenLpVerse> openLpVerses(const QString &xml)
{
    std::vector<OpenLpVerse> verses;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QLatin1String("verse"))
            continue;
        const auto attributes = reader.attributes();
        QString type = attributes.value(QStringLiteral("type")).toString();
        QString number = attributes.value(QStringLiteral("label")).toString();
        // Newer files name verses "v1", "c1".
        if (const QString name = attributes.value(QStringLiteral("name")).toString(); type.isEmpty() && !name.isEmpty()) {
            type = name.left(1);
            number = name.mid(1);
        }
        verses.push_back({type, number, reader.readElementText(QXmlStreamReader::IncludeChildElements)});
    }
    return verses;
}

ParsedSong openLpSong(const QString &title, const QString &lyricsXml, const QString &verseOrder)
{
    ParsedSong song;
    song.title = title.trimmed();
    const auto verses = openLpVerses(lyricsXml);
    QHash<QString, int> typeCounts;
    for (const OpenLpVerse &v : verses)
        ++typeCounts[v.type.toLower()];
    QHash<QString, QString> labelOf; // "v1" -> "Verse 1"
    for (const OpenLpVerse &v : verses) {
        ParsedSection section{openLpLabel(v.type, v.number, typeCounts.value(v.type.toLower())), {}};
        section.label = uniqueLabel(section.label, song);
        // "[---]" is OpenLP's slide break inside a verse.
        for (const QString &part : v.text.split(QStringLiteral("[---]"))) {
            auto lines = linesOf(part);
            if (!lines.empty())
                section.blocks.push_back(std::move(lines));
        }
        if (section.blocks.empty())
            continue;
        labelOf.insert(v.type.toLower() + (v.number.isEmpty() ? QStringLiteral("1") : v.number), section.label);
        song.sections.push_back(std::move(section));
    }
    for (const QString &key : verseOrder.toLower().split(u' ', Qt::SkipEmptyParts)) {
        const QString full = key.size() == 1 ? key + u'1' : key;
        if (labelOf.contains(full))
            song.order << labelOf.value(full);
        else
            song.warnings << QStringLiteral("unknown part \"%1\" in the verse order").arg(key);
    }
    if (song.order.isEmpty()) {
        for (const ParsedSection &s : song.sections)
            song.order << s.label;
    }
    return song;
}

// ---- RTF -------------------------------------------------------------------

bool hiddenDestination(const QByteArray &word)
{
    static const QList<QByteArray> hidden{"fonttbl", "colortbl", "stylesheet", "info", "pict", "header", "footer",
                                          "listtable", "listoverridetable", "themedata", "colorschememapping",
                                          "latentstyles", "datastore", "xmlnstbl", "rsidtbl", "generator"};
    return hidden.contains(word);
}

} // namespace

QString rtfToText(const QByteArray &rtf)
{
    QString out;
    QByteArray pending; // Windows-1252 bytes from \'hh, decoded together
    auto flush = [&] {
        if (pending.isEmpty())
            return;
        out += decodeWindows1252(pending);
        pending.clear();
    };
    struct State
    {
        bool skip = false;
        int ucSkip = 1;
    };
    std::vector<State> stack{State{}};
    int skipChars = 0; // after \uN: the fallback characters to drop
    for (qsizetype i = 0; i < rtf.size(); ++i) {
        const char c = rtf[i];
        State &state = stack.back();
        if (c == '{') {
            flush();
            stack.push_back(state);
            continue;
        }
        if (c == '}') {
            flush();
            if (stack.size() > 1)
                stack.pop_back();
            continue;
        }
        if (c == '\\') {
            if (i + 1 >= rtf.size())
                break;
            const char next = rtf[i + 1];
            if (next == '\\' || next == '{' || next == '}') {
                ++i;
                if (!state.skip && skipChars == 0) {
                    flush();
                    out += QLatin1Char(next);
                } else if (skipChars > 0) {
                    --skipChars;
                }
                continue;
            }
            if (next == '\'') {
                const QByteArray hex = rtf.mid(i + 2, 2);
                i += 3;
                if (skipChars > 0) {
                    --skipChars;
                    continue;
                }
                if (!state.skip)
                    pending += char(hex.toInt(nullptr, 16));
                continue;
            }
            if (next == '*') {
                state.skip = true; // {\*\destination ...}: not text
                ++i;
                continue;
            }
            if (next == '\n' || next == '\r') { // "\<newline>" is a paragraph
                ++i;
                if (!state.skip) {
                    flush();
                    out += u'\n';
                }
                continue;
            }
            // A control word: letters, an optional number, one space eaten.
            qsizetype j = i + 1;
            while (j < rtf.size() && std::isalpha(uchar(rtf[j])))
                ++j;
            const QByteArray word = rtf.mid(i + 1, j - i - 1);
            qsizetype k = j;
            if (k < rtf.size() && (rtf[k] == '-' || std::isdigit(uchar(rtf[k])))) {
                ++k;
                while (k < rtf.size() && std::isdigit(uchar(rtf[k])))
                    ++k;
            }
            const QByteArray number = rtf.mid(j, k - j);
            if (k < rtf.size() && rtf[k] == ' ')
                ++k;
            i = k - 1;
            if (word.isEmpty()) {
                ++i; // a control symbol such as \~ or \-
                if (next == '~' && !state.skip) {
                    flush();
                    out += QChar(0x00A0);
                }
                continue;
            }
            if (hiddenDestination(word)) {
                state.skip = true;
                continue;
            }
            if (state.skip)
                continue;
            if (word == "par" || word == "line") {
                flush();
                out += u'\n';
            } else if (word == "tab") {
                flush();
                out += u'\t';
            } else if (word == "uc") {
                state.ucSkip = number.toInt();
            } else if (word == "u") {
                flush();
                int code = number.toInt();
                if (code < 0)
                    code += 65536;
                out += QChar(char16_t(code));
                skipChars = state.ucSkip;
            }
            continue;
        }
        if (c == '\r' || c == '\n')
            continue;
        if (state.skip)
            continue;
        if (skipChars > 0) {
            --skipChars;
            continue;
        }
        pending += c;
    }
    flush();
    // Trailing spaces on lines and blank lines at the ends are noise.
    QStringList lines = out.split(u'\n');
    for (QString &line : lines)
        line = line.trimmed();
    while (!lines.isEmpty() && lines.front().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.back().isEmpty())
        lines.removeLast();
    return lines.join(u'\n');
}

LibraryKind libraryKind(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !file.read(16).startsWith("SQLite format 3"))
        return LibraryKind::None;
    file.close();
    ForeignDb db(path);
    if (!db.isOpen())
        return LibraryKind::None;
    const QStringList tables = db.tables();
    if (tables.contains(QStringLiteral("songs")) && tables.contains(QStringLiteral("authors_songs")))
        return LibraryKind::OpenLP;
    if (tables.contains(QStringLiteral("song")) && QFileInfo::exists(QFileInfo(path).dir().filePath(QStringLiteral("SongWords.db"))))
        return LibraryKind::EasyWorship;
    return LibraryKind::None;
}

Result<std::vector<LibrarySong>> readOpenLP(const QString &path)
{
    ForeignDb db(path);
    if (!db.isOpen())
        return invalid(QStringLiteral("could not open the OpenLP library"));
    QHash<int, QStringList> authors;
    {
        QSqlQuery query(db.db());
        if (query.exec(QStringLiteral("SELECT s.song_id, a.display_name FROM authors_songs s JOIN authors a ON a.id = s.author_id "
                                      "ORDER BY s.song_id, a.id"))) {
            while (query.next())
                authors[query.value(0).toInt()] << query.value(1).toString();
        }
    }
    QSqlQuery query(db.db());
    if (!query.exec(QStringLiteral("SELECT id, title, lyrics, verse_order, copyright, ccli_number FROM songs ORDER BY id")))
        return invalid(QStringLiteral("not an OpenLP library: %1").arg(query.lastError().text()));
    std::vector<LibrarySong> songs;
    while (query.next()) {
        ParsedSong song = openLpSong(query.value(1).toString(), query.value(2).toString(), query.value(3).toString());
        song.authors = authors.value(query.value(0).toInt());
        song.copyright = query.value(4).toString().trimmed();
        song.ccli = query.value(5).toString().trimmed();
        songs.push_back({QStringLiteral("song %1").arg(query.value(0).toInt()), std::move(song)});
    }
    return songs;
}

Result<std::vector<LibrarySong>> readEasyWorship(const QString &songsDb)
{
    ForeignDb songs(songsDb);
    ForeignDb words(QFileInfo(songsDb).dir().filePath(QStringLiteral("SongWords.db")));
    if (!songs.isOpen() || !words.isOpen())
        return invalid(QStringLiteral("could not open the EasyWorship library (Songs.db and SongWords.db)"));
    QHash<qint64, QByteArray> lyrics;
    {
        QSqlQuery query(words.db());
        if (!query.exec(QStringLiteral("SELECT song_id, words FROM word")))
            return invalid(QStringLiteral("not an EasyWorship library: %1").arg(query.lastError().text()));
        while (query.next())
            lyrics.insert(query.value(0).toLongLong(), query.value(1).toByteArray());
    }
    QSqlQuery query(songs.db());
    if (!query.exec(QStringLiteral("SELECT rowid, title, author, copyright, administrator, reference_number FROM song ORDER BY rowid")))
        return invalid(QStringLiteral("not an EasyWorship library: %1").arg(query.lastError().text()));
    std::vector<LibrarySong> out;
    while (query.next()) {
        const qint64 id = query.value(0).toLongLong();
        const QString title = query.value(1).toString().trimmed();
        // EasyWorship lyrics: section names on their own line ("Verse 1"),
        // a blank line between slides.
        const QString text = slideBreaksFromBlankLines(rtfToText(lyrics.value(id)));
        auto parsed = parseTextSong(text, title);
        LibrarySong song{QStringLiteral("song %1").arg(id), {}};
        if (!parsed.ok()) {
            song.song.title = title;
            song.song.warnings << parsed.error().message;
        } else {
            song.song = parsed.value();
        }
        song.song.title = title.isEmpty() ? song.song.title : title;
        const QString author = query.value(2).toString().trimmed();
        if (!author.isEmpty())
            song.song.authors = author.split(QRegularExpression(QStringLiteral(R"(\s*[;,/]\s*|\s+and\s+)")), Qt::SkipEmptyParts);
        song.song.copyright = query.value(3).toString().trimmed();
        if (const QString admin = query.value(4).toString().trimmed(); !admin.isEmpty() && !song.song.copyright.contains(admin))
            song.song.copyright = song.song.copyright.isEmpty() ? admin : song.song.copyright + QStringLiteral(" (admin. %1)").arg(admin);
        song.song.ccli = query.value(5).toString().trimmed();
        out.push_back(std::move(song));
    }
    return out;
}

namespace {

void descendants(const QDomElement &e, const QString &tag, QList<QDomElement> &out)
{
    for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (c.tagName() == tag)
            out << c;
        else
            descendants(c, tag, out);
    }
}

// The text of a ProPresenter text element: plain text (6.1 and later), else
// its RTF (5 and 6), both base64.
QString textOf(const QDomElement &element)
{
    for (QDomElement c = element.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (c.attribute(QStringLiteral("rvXMLIvarName")) == QLatin1String("PlainText"))
            return QString::fromUtf8(QByteArray::fromBase64(c.text().trimmed().toLatin1()));
    }
    for (QDomElement c = element.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (c.attribute(QStringLiteral("rvXMLIvarName")) == QLatin1String("RTFData"))
            return rtfToText(QByteArray::fromBase64(c.text().trimmed().toLatin1()));
    }
    if (element.hasAttribute(QStringLiteral("RTFData")))
        return rtfToText(QByteArray::fromBase64(element.attribute(QStringLiteral("RTFData")).toLatin1()));
    return {};
}

} // namespace

Result<ParsedSong> parseProPresenter(const QByteArray &xml, const QString &fallbackTitle)
{
    QDomDocument doc;
    if (!doc.setContent(xml) || doc.documentElement().tagName() != QLatin1String("RVPresentationDocument"))
        return invalid(QStringLiteral("not a ProPresenter 5 or 6 document"));
    const QDomElement root = doc.documentElement();
    ParsedSong song;
    song.title = root.attribute(QStringLiteral("CCLISongTitle")).trimmed();
    if (song.title.isEmpty())
        song.title = fallbackTitle;
    for (const char *attribute : {"CCLIAuthor", "CCLIArtistCredits"}) {
        const QString author = root.attribute(QLatin1String(attribute)).trimmed();
        if (!author.isEmpty() && !song.authors.contains(author))
            song.authors << author;
    }
    const QString year = root.attribute(QStringLiteral("CCLICopyrightYear")).trimmed();
    const QString publisher = root.attribute(QStringLiteral("CCLIPublisher")).trimmed();
    song.copyright = QStringList{year, publisher}.join(u' ').trimmed();
    song.ccli = root.attribute(QStringLiteral("CCLISongNumber")).trimmed();

    QList<QDomElement> groups;
    descendants(root, QStringLiteral("RVSlideGrouping"), groups);
    QHash<QString, QString> labelOf; // group uuid -> label
    for (const QDomElement &group : groups) {
        const QString name = group.attribute(QStringLiteral("name")).trimmed();
        QString label = sectionLabelFromHeader(name);
        if (label.isEmpty())
            label = name.isEmpty() ? QStringLiteral("Verse") : name;
        ParsedSection section{uniqueLabel(label, song), {}};
        QList<QDomElement> slides;
        descendants(group, QStringLiteral("RVDisplaySlide"), slides);
        for (const QDomElement &slide : slides) {
            QList<QDomElement> texts;
            descendants(slide, QStringLiteral("RVTextElement"), texts);
            QStringList slideText;
            for (const QDomElement &t : texts)
                slideText << textOf(t);
            auto lines = linesOf(slideText.join(u'\n'));
            if (!lines.empty())
                section.blocks.push_back(std::move(lines));
        }
        if (section.blocks.empty())
            continue;
        labelOf.insert(group.attribute(QStringLiteral("uuid")).toUpper(), section.label);
        song.sections.push_back(std::move(section));
    }
    if (song.sections.empty())
        return invalid(QStringLiteral("the document has no lyrics"));

    // The first arrangement, if any; else the groups in order.
    QList<QDomElement> arrangements;
    descendants(root, QStringLiteral("RVSongArrangement"), arrangements);
    if (!arrangements.isEmpty()) {
        QList<QDomElement> ids;
        for (const char *tag : {"NSString", "NSMutableString"})
            descendants(arrangements.front(), QLatin1String(tag), ids);
        for (const QDomElement &id : ids) {
            const QString label = labelOf.value(id.text().trimmed().toUpper());
            if (!label.isEmpty())
                song.order << label;
        }
    }
    if (song.order.isEmpty()) {
        for (const ParsedSection &s : song.sections)
            song.order << s.label;
    }
    return song;
}

} // namespace lumen::core::songs
