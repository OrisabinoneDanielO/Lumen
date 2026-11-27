#pragma once

#include "lumen/core/data/database.h"
#include "lumen/core/result.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <vector>

namespace lumen::app {

struct MediaRow
{
    QString presentationId;
    QString assetId;
    QString title;
    QString kind;      // "video" or "image"
    QString path;
    QString thumbnail; // a small JPEG made on import
    qint64 durationMs = 0;
    bool loop = false; // short videos loop by default (motion backgrounds)
};

struct MediaImportReport
{
    int imported = 0;
    int skipped = 0;
    int failed = 0;
    QStringList messages;
    QString summary() const;
};

// Videos and pictures in the library. Importing reads each file's length and
// makes a thumbnail (decoding one frame here in the UI process; playback
// itself happens in the render process), fingerprints it to skip duplicates,
// and stores it as a media asset plus a media presentation that can go in a
// plan. Files stay where they are; the library refers to them by path.
class MediaLibrary
{
public:
    MediaLibrary(core::data::Database &db, QString thumbnailFolder);

    static bool isVideo(const QString &path);
    static bool isImage(const QString &path);

    MediaImportReport importFiles(const QStringList &paths);
    core::Result<std::vector<MediaRow>> list() const;
    // Takes a video or picture out of the library (and out of the plans
    // that use it): the file itself stays where it is; its thumbnail goes.
    // Returns how many plan items were removed with it.
    core::Result<int> remove(const QString &presentationId);

    // Size plus the first and last megabyte: quick, and enough to tell files
    // apart (and to find a moved file again).
    static QString fingerprint(const QString &path);

    // The video's length and one early frame (waits at most timeoutMs).
    struct Probe
    {
        qint64 durationMs = 0;
        QImage frame;
    };
    static Probe probeVideo(const QString &path, int timeoutMs = 5000);

private:
    core::data::Database &m_db;
    QString m_thumbnails;
};

} // namespace lumen::app
