#pragma once

#include "lumen/core/data/database.h"
#include "lumen/core/result.h"

#include <QDateTime>
#include <QString>

#include <vector>

namespace lumen::app {

// Scheduled local backups of the library (batch 11). A backup is a complete
// copy of the library database in a folder of the user's choosing (by
// default "backups" beside the library; a USB stick or second drive is
// better). One is made when a day has passed since the last; the newest
// `keep` are kept.
//
// Backups hold the library index and plans. Media files stay where they
// are, referenced by path and content hash; a .lumen export bundles them.
class Backups
{
public:
    Backups(core::data::Database &db, QString folder, int keep = 14);

    struct Entry
    {
        QString path;
        QDateTime made; // UTC
        qint64 bytes = 0;
    };

    // Newest first.
    std::vector<Entry> list() const;

    core::Result<Entry> backUpNow();

    // Backs up if the newest backup is older than a day (or there is none).
    // Returns true if one was made.
    core::Result<bool> backUpIfDue(const QDateTime &now = QDateTime::currentDateTimeUtc());

    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);

    // Restoring replaces the open library, so it happens on the next start:
    // scheduleRestore() records the choice beside the library and
    // applyPendingRestore(), called before the library is opened, puts the
    // backup in place (the replaced library is kept as
    // "library-before-restore-<time>.db").
    static core::Status scheduleRestore(const QString &libraryPath, const QString &backupPath);
    // Returns true if a restore was applied.
    static core::Result<bool> applyPendingRestore(const QString &libraryPath);

private:
    void prune();

    core::data::Database &m_db;
    QString m_folder;
    int m_keep;
};

} // namespace lumen::app
