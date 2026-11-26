#include "lumen/core/songs/songlibrary.h"

#include "lumen/core/data/repository.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace lumen::core::songs {

using namespace data;

namespace {

Error sqlFailure(const QSqlQuery &query, const QString &context)
{
    return Error{Error::Code::Database, context + QStringLiteral(": ") + query.lastError().text()};
}

// The first lyric line of the song's first section (as stored).
QString firstLine(const ParsedSong &song)
{
    for (const ParsedSection &section : song.sections) {
        const QStringList texts = section.texts();
        if (!texts.isEmpty())
            return texts.front();
    }
    return {};
}

} // namespace

QString ImportReport::summary() const
{
    QStringList parts{QStringLiteral("%1 imported").arg(imported)};
    if (skipped > 0)
        parts << QStringLiteral("%1 skipped").arg(skipped);
    if (failed > 0)
        parts << QStringLiteral("%1 failed").arg(failed);
    return parts.join(QStringLiteral(", "));
}

SongLibrary::SongLibrary(Database &db, int linesPerSlide)
    : m_db(db), m_linesPerSlide(qMax(1, linesPerSlide))
{}

Result<ParsedSong> SongLibrary::parseFile(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Error{Error::Code::Invalid, file.errorString()};
    const QByteArray bytes = file.readAll();
    const QByteArray head = bytes.left(512).trimmed();
    const bool xml = path.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)
                     || head.startsWith("<?xml") || head.startsWith("<song");
    if (xml)
        return parseOpenLyrics(bytes);
    return parseTextSong(decodeText(bytes), titleFromFileName(path));
}

Result<QString> SongLibrary::defaultLibraryId()
{
    auto libraries = Repository<Library>(m_db).list();
    if (!libraries.ok())
        return libraries.error();
    if (!libraries.value().empty())
        return libraries.value().front().id;
    auto created = Repository<Library>(m_db).create({.name = "Main library"});
    if (!created.ok())
        return created.error();
    return created.value().id;
}

Result<ImportedSong> SongLibrary::add(const ParsedSong &song, const QString &libraryId)
{
    // Already in the library?
    {
        QSqlQuery query(m_db.connection());
        query.prepare(QStringLiteral("SELECT presentation_id FROM song_index WHERE title = ? AND first_line = ?"));
        query.addBindValue(normaliseForSearch(song.title));
        query.addBindValue(normaliseForSearch(firstLine(song)));
        if (!query.exec())
            return sqlFailure(query, QStringLiteral("Checking for duplicates"));
        if (query.next())
            return ImportedSong{query.value(0).toString(), song.title, true};
    }

    QString library = libraryId;
    if (library.isEmpty()) {
        auto id = defaultLibraryId();
        if (!id.ok())
            return id.error();
        library = id.value();
    }

    QJsonObject metadata;
    if (!song.authors.isEmpty())
        metadata.insert(QStringLiteral("author"), song.authors.join(QStringLiteral(", ")));
    if (!song.copyright.isEmpty())
        metadata.insert(QStringLiteral("copyright"), song.copyright);
    if (!song.ccli.isEmpty())
        metadata.insert(QStringLiteral("ccliNumber"), song.ccli);
    if (!song.key.isEmpty())
        metadata.insert(QStringLiteral("key"), song.key);
    if (!song.tempo.isEmpty())
        metadata.insert(QStringLiteral("tempo"), song.tempo);

    QString presentationId;
    const Status stored = m_db.inTransaction([&]() -> Status {
        auto presentation = Repository<Presentation>(m_db).create(
            {.libraryId = library, .kind = PresentationKind::Song, .title = song.title, .metadata = metadata});
        if (!presentation.ok())
            return presentation.error();
        presentationId = presentation.value().id;

        QHash<QString, QString> groupIds; // label -> id
        int groupPosition = 0;
        for (const ParsedSection &section : song.sections) {
            auto group = Repository<Group>(m_db).create(
                {.presentationId = presentationId, .position = groupPosition++, .label = section.label});
            if (!group.ok())
                return group.error();
            groupIds.insert(section.label, group.value().id);

            int slidePosition = 0;
            for (const auto &block : section.blocks) {
                for (const auto &lines : splitIntoSlides(block, m_linesPerSlide)) {
                    QStringList texts;
                    QStringList chords;
                    for (const LyricLine &line : lines) {
                        texts << line.text;
                        if (!line.chords.isEmpty())
                            chords << line.chords;
                    }
                    auto slide = Repository<Slide>(m_db).create(
                        {.groupId = group.value().id, .position = slidePosition++,
                         .chords = chords.join(QStringLiteral(" | "))});
                    if (!slide.ok())
                        return slide.error();
                    auto element = Repository<Element>(m_db).create(
                        {.slideId = slide.value().id, .type = ElementType::Text,
                         .frame = QRectF(96, 96, 1728, 888), .content = texts.join(u'\n')});
                    if (!element.ok())
                        return element.error();
                }
            }
        }

        QStringList order;
        for (const QString &label : song.order) {
            if (groupIds.contains(label))
                order << groupIds.value(label);
        }
        auto arrangement = Repository<Arrangement>(m_db).create(
            {.presentationId = presentationId, .name = "Default", .isDefault = true, .groupIds = order});
        if (!arrangement.ok())
            return arrangement.error();
        return reindex(presentationId);
    });
    if (!stored.ok())
        return stored.error();
    return ImportedSong{presentationId, song.title, false};
}

ImportReport SongLibrary::importFiles(const QStringList &paths)
{
    QStringList files;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(path, {QStringLiteral("*.txt"), QStringLiteral("*.xml")}, QDir::Files,
                            QDirIterator::Subdirectories);
            QStringList found;
            while (it.hasNext())
                found << it.next();
            found.sort();
            files << found;
        } else {
            files << path;
        }
    }

    ImportReport report;
    for (const QString &file : files) {
        const QString name = QFileInfo(file).fileName();
        auto parsed = parseFile(file);
        if (!parsed.ok()) {
            ++report.failed;
            report.messages << QStringLiteral("%1: %2").arg(name, parsed.error().message);
            continue;
        }
        auto added = add(parsed.value());
        if (!added.ok()) {
            ++report.failed;
            report.messages << QStringLiteral("%1: %2").arg(name, added.error().message);
        } else if (added.value().duplicate) {
            ++report.skipped;
            report.messages << QStringLiteral("%1: \"%2\" is already in the library").arg(name, added.value().title);
        } else {
            ++report.imported;
            report.presentationIds << added.value().presentationId;
        }
    }
    return report;
}

Status SongLibrary::reindex(const QString &presentationId)
{
    auto presentation = Repository<Presentation>(m_db).get(presentationId);
    if (!presentation.ok())
        return presentation.error();
    auto groups = Repository<Group>(m_db).listChildren(presentationId);
    if (!groups.ok())
        return groups.error();

    QStringList lyrics;
    QString first;
    for (const Group &group : groups.value()) {
        auto slides = Repository<Slide>(m_db).listChildren(group.id);
        if (!slides.ok())
            return slides.error();
        for (const Slide &slide : slides.value()) {
            auto elements = Repository<Element>(m_db).listChildren(slide.id);
            if (!elements.ok())
                return elements.error();
            for (const Element &element : elements.value()) {
                if (element.type != ElementType::Text || element.content.isEmpty())
                    continue;
                lyrics << element.content;
                if (first.isEmpty())
                    first = element.content.section(u'\n', 0, 0);
            }
        }
    }

    const QJsonObject &meta = presentation.value().metadata;
    QSqlQuery query(m_db.connection());
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO song_index "
                                 "(presentation_id, title, author, ccli, lyrics, first_line) "
                                 "VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(presentationId);
    query.addBindValue(normaliseForSearch(presentation.value().title));
    query.addBindValue(normaliseForSearch(meta.value(QStringLiteral("author")).toString()));
    query.addBindValue(normaliseForSearch(meta.value(QStringLiteral("ccliNumber")).toString()));
    query.addBindValue(normaliseForSearch(lyrics.join(u' ')));
    query.addBindValue(normaliseForSearch(first));
    if (!query.exec())
        return sqlFailure(query, QStringLiteral("Indexing song"));
    return success();
}

Status SongLibrary::indexMissing()
{
    QStringList missing;
    {
        QSqlQuery query(m_db.connection());
        if (!query.exec(QStringLiteral("SELECT id FROM presentations WHERE kind = 'song' AND id NOT IN "
                                       "(SELECT presentation_id FROM song_index)")))
            return sqlFailure(query, QStringLiteral("Finding unindexed songs"));
        while (query.next())
            missing << query.value(0).toString();
    }
    return m_db.inTransaction([&]() -> Status {
        for (const QString &id : missing) {
            if (auto s = reindex(id); !s.ok())
                return s;
        }
        return success();
    });
}

Result<std::vector<SongHit>> SongLibrary::search(const QString &query, int limit) const
{
    const QStringList words = normaliseForSearch(query).split(u' ', Qt::SkipEmptyParts);
    auto pattern = [](QString word) {
        word.replace(u'\\', QStringLiteral("\\\\")).replace(u'%', QStringLiteral("\\%")).replace(u'_', QStringLiteral("\\_"));
        return QStringLiteral("% ") + word + u'%';
    };

    QString sql = QStringLiteral("SELECT p.id, p.title, p.metadata FROM presentations p "
                                 "LEFT JOIN song_index s ON s.presentation_id = p.id WHERE p.kind = 'song'");
    for (qsizetype i = 0; i < words.size(); ++i) {
        sql += QStringLiteral(" AND (' ' || s.title || ' ' || s.author || ' ' || s.ccli || ' ' || s.lyrics) "
                              "LIKE ? ESCAPE '\\'");
    }
    sql += words.isEmpty() ? QStringLiteral(" ORDER BY p.title COLLATE NOCASE, p.id LIMIT ?")
                           : QStringLiteral(" ORDER BY (' ' || s.title) LIKE ? ESCAPE '\\' DESC, "
                                            "p.title COLLATE NOCASE, p.id LIMIT ?");

    QSqlQuery q(m_db.connection());
    q.setForwardOnly(true);
    if (!q.prepare(sql))
        return sqlFailure(q, QStringLiteral("Searching songs"));
    for (const QString &word : words)
        q.addBindValue(pattern(word));
    if (!words.isEmpty())
        q.addBindValue(pattern(words.front()));
    q.addBindValue(limit);
    if (!q.exec())
        return sqlFailure(q, QStringLiteral("Searching songs"));

    std::vector<SongHit> hits;
    while (q.next()) {
        const QJsonObject meta = QJsonDocument::fromJson(q.value(2).toString().toUtf8()).object();
        hits.push_back({q.value(0).toString(), q.value(1).toString(),
                        meta.value(QStringLiteral("author")).toString(),
                        meta.value(QStringLiteral("copyright")).toString(),
                        meta.value(QStringLiteral("key")).toString()});
    }
    return hits;
}

} // namespace lumen::core::songs
