#include "freesongs.h"

#include "lumen/core/songs/songimport.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace lumen::app {

using namespace core;
using namespace core::songs;

QByteArray builtInFreeSongs()
{
    QFile file(QStringLiteral(":/lumen/songs/songs.json"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int builtInFreeSongsVersion()
{
    return QJsonDocument::fromJson(builtInFreeSongs()).object().value(QStringLiteral("version")).toInt();
}

Result<int> addFreeSongs(data::Database &db, SongLibrary &songs, const QByteArray &collection)
{
    const QJsonObject root = QJsonDocument::fromJson(collection).object();
    if (!root.contains(QStringLiteral("songs")))
        return Error{Error::Code::Invalid, QStringLiteral("The free song collection could not be read")};
    int added = 0;
    const Status stored = db.inTransaction([&]() -> Status {
        for (const QJsonValue &value : root.value(QStringLiteral("songs")).toArray()) {
            const QJsonObject s = value.toObject();
            const QString body = s.value(QStringLiteral("body")).toString();
            const QString title = s.value(QStringLiteral("title")).toString();
            auto parsed = s.value(QStringLiteral("format")).toString() == QLatin1String("openlyrics")
                              ? parseOpenLyrics(body.toUtf8())
                              : parseTextSong(body, title);
            if (!parsed.ok() || parsed.value().sections.empty())
                continue;
            ParsedSong &song = parsed.value();
            song.title = title;
            song.authors = QStringList{s.value(QStringLiteral("writer")).toString()};
            song.authors.removeAll(QString());
            song.copyright = s.value(QStringLiteral("copyright")).toString();
            if (const QString ccli = s.value(QStringLiteral("ccli")).toString(); !ccli.isEmpty())
                song.ccli = ccli;
            if (const QString key = s.value(QStringLiteral("key")).toString(); !key.isEmpty())
                song.key = key;
            auto one = songs.add(song);
            if (one.ok() && !one.value().duplicate)
                ++added;
        }
        return success();
    });
    if (!stored.ok())
        return stored.error();
    return added;
}

} // namespace lumen::app
