#include "medialibrary.h"
#include <QSqlError>
#include <QSqlQuery>

#include "lumen/core/data/repository.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

namespace lumen::app {

using namespace core;
using namespace core::data;

namespace {

const QStringList videoSuffixes{"mp4", "m4v", "mov", "webm", "mkv", "avi", "mpg", "mpeg", "wmv"};
const QStringList imageSuffixes{"png", "jpg", "jpeg", "bmp", "gif", "webp"};
const QStringList audioSuffixes{"mp3", "wav", "m4a", "aac", "ogg", "oga", "flac", "opus", "wma"};

QString lengthText(qint64 ms)
{
    const qint64 seconds = (ms + 500) / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

QString MediaImportReport::summary() const
{
    QStringList parts{QStringLiteral("%1 imported").arg(imported)};
    if (skipped > 0)
        parts << QStringLiteral("%1 skipped").arg(skipped);
    if (failed > 0)
        parts << QStringLiteral("%1 failed").arg(failed);
    return parts.join(QStringLiteral(", "));
}

MediaLibrary::MediaLibrary(Database &db, QString thumbnailFolder)
    : m_db(db), m_thumbnails(std::move(thumbnailFolder))
{
    QDir().mkpath(m_thumbnails);
}

bool MediaLibrary::isVideo(const QString &path)
{
    return videoSuffixes.contains(QFileInfo(path).suffix().toLower());
}

bool MediaLibrary::isAudio(const QString &path)
{
    return audioSuffixes.contains(QFileInfo(path).suffix().toLower());
}

qint64 MediaLibrary::probeAudio(const QString &path, int timeoutMs)
{
    QMediaPlayer player; // no audio output: silent
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&player, &QMediaPlayer::durationChanged, &loop, [&](qint64 duration) {
        if (duration > 0)
            loop.quit();
    });
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &loop, [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::InvalidMedia)
            loop.quit();
    });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop, &QEventLoop::quit);
    player.setSource(QUrl::fromLocalFile(path));
    timeout.start(timeoutMs);
    if (player.duration() <= 0)
        loop.exec();
    return player.duration();
}

bool MediaLibrary::isImage(const QString &path)
{
    return imageSuffixes.contains(QFileInfo(path).suffix().toLower());
}

QString MediaLibrary::fingerprint(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    constexpr qint64 chunk = 1024 * 1024;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(file.size()));
    hash.addData(file.read(chunk));
    if (file.size() > 2 * chunk) {
        file.seek(file.size() - chunk);
        hash.addData(file.read(chunk));
    }
    return QStringLiteral("sha256:") + QString::fromLatin1(hash.result().toHex());
}

MediaLibrary::Probe MediaLibrary::probeVideo(const QString &path, int timeoutMs)
{
    Probe probe;
    QMediaPlayer player; // no audio output: silent
    QVideoSink sink;
    player.setVideoSink(&sink);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop, [&](const QVideoFrame &frame) {
        if (probe.frame.isNull() && frame.isValid())
            probe.frame = frame.toImage();
        if (!probe.frame.isNull() && probe.durationMs > 0)
            loop.quit();
    });
    QObject::connect(&player, &QMediaPlayer::durationChanged, &loop, [&](qint64 duration) {
        probe.durationMs = duration;
        if (!probe.frame.isNull() && probe.durationMs > 0)
            loop.quit();
    });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop, &QEventLoop::quit);
    player.setSource(QUrl::fromLocalFile(path));
    player.play();
    timeout.start(timeoutMs);
    loop.exec();
    player.stop();
    if (probe.durationMs <= 0)
        probe.durationMs = player.duration();
    return probe;
}

MediaImportReport MediaLibrary::importFiles(const QStringList &paths)
{
    MediaImportReport report;
    auto library = Repository<Library>(m_db).list();
    QString libraryId = library.ok() && !library.value().empty() ? library.value().front().id : QString();
    if (libraryId.isEmpty()) {
        auto created = Repository<Library>(m_db).create({.name = "Main library"});
        if (!created.ok()) {
            report.failed = int(paths.size());
            report.messages << created.error().message;
            return report;
        }
        libraryId = created.value().id;
    }
    const auto existing = Repository<MediaAsset>(m_db).list();

    for (const QString &path : paths) {
        const QFileInfo info(path);
        const QString name = info.fileName();
        const bool audio = isAudio(path);
        const bool video = isVideo(path) || audio; // audio is played the same way, without a picture
        if (!info.isFile() || (!video && !isImage(path))) {
            ++report.failed;
            report.messages << QStringLiteral("%1: not a video, picture or audio file Lumen can play").arg(name);
            continue;
        }
        const QString hash = fingerprint(path);
        const bool duplicate = existing.ok() && std::any_of(existing.value().begin(), existing.value().end(),
                                                            [&](const MediaAsset &a) { return a.hash == hash; });
        if (hash.isEmpty() || duplicate) {
            ++report.skipped;
            report.messages << QStringLiteral("%1: already in the library").arg(name);
            continue;
        }

        QImage frame;
        qint64 duration = 0;
        if (audio) {
            duration = probeAudio(path);
            if (duration <= 0) {
                ++report.failed;
                report.messages << QStringLiteral("%1: the audio could not be read").arg(name);
                continue;
            }
        } else if (video) {
            const Probe probe = probeVideo(path);
            frame = probe.frame;
            duration = probe.durationMs;
            if (frame.isNull() && duration <= 0) {
                ++report.failed;
                report.messages << QStringLiteral("%1: the video could not be read").arg(name);
                continue;
            }
        } else {
            frame = QImage(path);
            if (frame.isNull()) {
                ++report.failed;
                report.messages << QStringLiteral("%1: the picture could not be read").arg(name);
                continue;
            }
        }
        const QString thumbnail = QDir(m_thumbnails).filePath(QString(hash).remove(0, 7).left(32) + QStringLiteral(".jpg"));
        if (!frame.isNull())
            frame.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(thumbnail, "JPG", 85);

        const Status stored = m_db.inTransaction([&]() -> Status {
            auto asset = Repository<MediaAsset>(m_db).create({.libraryId = libraryId,
                                                              .filePath = info.absoluteFilePath(),
                                                              .hash = hash,
                                                              .durationMs = duration,
                                                              .thumbnailPath = QFileInfo::exists(thumbnail) ? thumbnail : QString(),
                                                              .loop = video && !audio && duration > 0 && duration <= 60000});
            if (!asset.ok())
                return asset.error();
            const QString subtitle = audio   ? QStringLiteral("Audio · %1").arg(lengthText(duration))
                                     : video ? QStringLiteral("Video · %1").arg(lengthText(duration))
                                             : QStringLiteral("Picture");
            auto presentation = Repository<Presentation>(m_db).create({.libraryId = libraryId,
                                                                      .kind = PresentationKind::Media,
                                                                      .title = info.completeBaseName(),
                                                                      .metadata = {{"subtitle", subtitle}}});
            if (!presentation.ok())
                return presentation.error();
            auto group = Repository<Group>(m_db).create({.presentationId = presentation.value().id,
                                                         .label = audio ? QStringLiteral("Audio") : video ? QStringLiteral("Video") : QStringLiteral("Picture")});
            if (!group.ok())
                return group.error();
            auto slide = Repository<Slide>(m_db).create({.groupId = group.value().id});
            if (!slide.ok())
                return slide.error();
            auto element = Repository<Element>(m_db).create({.slideId = slide.value().id,
                                                             .type = video ? ElementType::Video : ElementType::Image,
                                                             .frame = QRectF(0, 0, 1920, 1080),
                                                             .mediaAssetId = asset.value().id});
            if (!element.ok())
                return element.error();
            return success();
        });
        if (!stored.ok()) {
            ++report.failed;
            report.messages << QStringLiteral("%1: %2").arg(name, stored.error().message);
            continue;
        }
        ++report.imported;
    }
    return report;
}

Result<std::vector<MediaRow>> MediaLibrary::list() const
{
    auto presentations = Repository<Presentation>(m_db).list();
    if (!presentations.ok())
        return presentations.error();
    std::vector<MediaRow> rows;
    for (const Presentation &p : presentations.value()) {
        if (p.kind != PresentationKind::Media)
            continue;
        const auto groups = Repository<Group>(m_db).listChildren(p.id);
        if (!groups.ok() || groups.value().empty())
            continue;
        const auto slides = Repository<Slide>(m_db).listChildren(groups.value().front().id);
        if (!slides.ok() || slides.value().empty())
            continue;
        const auto elements = Repository<Element>(m_db).listChildren(slides.value().front().id);
        if (!elements.ok())
            continue;
        for (const Element &element : elements.value()) {
            if (element.mediaAssetId.isEmpty())
                continue;
            auto asset = Repository<MediaAsset>(m_db).get(element.mediaAssetId);
            if (!asset.ok())
                continue;
            rows.push_back({p.id, asset.value().id, p.title,
                            element.type != ElementType::Video ? QStringLiteral("image") : isAudio(asset.value().filePath) ? QStringLiteral("audio") : QStringLiteral("video"),
                            asset.value().filePath, asset.value().thumbnailPath, asset.value().durationMs,
                            asset.value().loop});
            break;
        }
    }
    return rows;
}

Result<int> MediaLibrary::remove(const QString &presentationId)
{
    auto presentation = Repository<Presentation>(m_db).get(presentationId);
    if (!presentation.ok())
        return presentation.error();
    if (presentation.value().kind != PresentationKind::Media)
        return Error{Error::Code::Invalid, QStringLiteral("Not a video or picture")};
    int planItems = 0;
    QStringList thumbnails;
    const Status removed = m_db.inTransaction([&]() -> Status {
        // Its files (the asset), to forget unless something else uses them.
        QStringList assets;
        QSqlQuery used(m_db.connection());
        used.prepare(QStringLiteral("SELECT DISTINCT e.media_asset_id FROM elements e JOIN slides s ON s.id = e.slide_id "
                                    "JOIN slide_groups g ON g.id = s.group_id WHERE g.presentation_id = ? "
                                    "AND e.media_asset_id IS NOT NULL"));
        used.addBindValue(presentationId);
        if (!used.exec())
            return Error{Error::Code::Database, used.lastError().text()};
        while (used.next())
            assets << used.value(0).toString();
        QSqlQuery items(m_db.connection());
        items.prepare(QStringLiteral("DELETE FROM plan_items WHERE presentation_id = ?"));
        items.addBindValue(presentationId);
        if (!items.exec())
            return Error{Error::Code::Database, items.lastError().text()};
        planItems = items.numRowsAffected();
        if (auto gone = Repository<Presentation>(m_db).remove(presentationId); !gone.ok())
            return gone;
        for (const QString &asset : assets) {
            QSqlQuery still(m_db.connection());
            still.prepare(QStringLiteral("SELECT COUNT(*) FROM elements WHERE media_asset_id = ?"));
            still.addBindValue(asset);
            if (!still.exec() || !still.next())
                return Error{Error::Code::Database, still.lastError().text()};
            if (still.value(0).toInt() > 0)
                continue; // a slide made in the editor uses it too
            if (auto row = Repository<MediaAsset>(m_db).get(asset); row.ok() && !row.value().thumbnailPath.isEmpty())
                thumbnails << row.value().thumbnailPath;
            if (auto gone = Repository<MediaAsset>(m_db).remove(asset); !gone.ok())
                return gone;
        }
        return success();
    });
    if (!removed.ok())
        return removed.error();
    for (const QString &thumbnail : thumbnails) {
        if (thumbnail.startsWith(m_thumbnails))
            QFile::remove(thumbnail);
    }
    return planItems;
}

} // namespace lumen::app
