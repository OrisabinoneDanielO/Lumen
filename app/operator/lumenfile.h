#pragma once

#include "lumen/core/data/database.h"
#include "lumen/core/result.h"

#include <QString>
#include <QStringList>

namespace lumen::app {

// .lumen files (batch 11): a service plan with everything it needs, for
// moving a plan to the booth PC, sharing it with another church or keeping
// it on a USB stick.
//
// The file is a zip archive (open, documented in docs/lumen-file.md):
//   lumen.json   the plan, its presentations (groups, slides, elements,
//                arrangements) and a list of media
//   media/...    the media files themselves and their thumbnails
//
// Importing adds a new plan. A presentation already in the library (same
// kind, title and slides) is reused rather than copied again; so is a media
// file with the same content hash. Other media are copied into mediaFolder.
namespace lumenfile {

constexpr int formatVersion = 1;

core::Status exportPlan(core::data::Database &db, const QString &planId, const QString &file);

struct ImportReport
{
    QString planId;
    QString planName;
    int presentationsAdded = 0;
    int presentationsReused = 0;
    int mediaAdded = 0;
    int mediaReused = 0;
    QStringList messages;
    QString summary() const;
};

core::Result<ImportReport> importFile(core::data::Database &db, const QString &file, const QString &mediaFolder);

} // namespace lumenfile
} // namespace lumen::app
