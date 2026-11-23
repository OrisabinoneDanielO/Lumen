#include "backups.h"

#include "lumen/core/data/repository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>

#include <algorithm>

namespace lumen::app {

using namespace core;
using namespace core::data;

namespace {

const QString prefix = QStringLiteral("library-");
const QString timeFormat = QStringLiteral("yyyyMMdd-HHmmss");

QString pendingFile(const QString &libraryPath)
{
    return libraryPath + QStringLiteral(".restore");
}

// A database file with its write-ahead log and shared-memory files.
QStringList databaseFiles(const QString &path)
{
    return {path, path + QStringLiteral("-wal"), path + QStringLiteral("-shm")};
}

} // namespace

Backups::Backups(Database &db, QString folder, int keep) : m_db(db), m_folder(std::move(folder)), m_keep(keep)
{
    QDir().mkpath(m_folder);
}

void Backups::setFolder(const QString &folder)
{
    m_folder = folder;
    QDir().mkpath(m_folder);
}

std::vector<Backups::Entry> Backups::list() const
{
    std::vector<Entry> entries;
    const QFileInfoList files = QDir(m_folder).entryInfoList({prefix + QStringLiteral("*.db")}, QDir::Files, QDir::Name);
    for (const QFileInfo &file : files) {
        const QString stamp = file.completeBaseName().mid(prefix.size());
        QDateTime made = QDateTime::fromString(stamp, timeFormat);
        if (!made.isValid())
            continue;
        made.setTimeZone(QTimeZone::UTC);
        entries.push_back({file.absoluteFilePath(), made, file.size()});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.made > b.made; });
    return entries;
}

Result<Backups::Entry> Backups::backUpNow()
{
    QDir().mkpath(m_folder);
    QDateTime now = QDateTime::currentDateTimeUtc();
    // Two backups in the same second: wait for the name to be free.
    QString path;
    for (int attempt = 0; attempt < 3; ++attempt) {
        path = QDir(m_folder).filePath(prefix + now.toString(timeFormat) + QStringLiteral(".db"));
        if (!QFileInfo::exists(path))
            break;
        now = now.addSecs(1);
    }
    const QString partial = path + QStringLiteral(".part");
    QFile::remove(partial);
    if (auto copied = m_db.backupTo(partial); !copied.ok())
        return copied.error();
    // Only complete backups get a backup's name.
    if (!QFile::rename(partial, path)) {
        QFile::remove(partial);
        return Error{Error::Code::Database, QStringLiteral("The backup could not be saved as %1").arg(path)};
    }
    // Recorded on the library, as the data model says.
    if (auto libraries = Repository<Library>(m_db).list(); libraries.ok()) {
        for (Library library : libraries.value()) {
            library.lastBackup = now;
            (void)Repository<Library>(m_db).update(library);
        }
    }
    prune();
    const QFileInfo info(path);
    return Entry{info.absoluteFilePath(), now, info.size()};
}

Result<bool> Backups::backUpIfDue(const QDateTime &now)
{
    const auto entries = list();
    if (!entries.empty() && entries.front().made.secsTo(now) < 24 * 3600)
        return false;
    auto made = backUpNow();
    if (!made.ok())
        return made.error();
    return true;
}

void Backups::prune()
{
    const auto entries = list();
    for (size_t i = size_t(std::max(m_keep, 1)); i < entries.size(); ++i)
        QFile::remove(entries[i].path);
}

Status Backups::scheduleRestore(const QString &libraryPath, const QString &backupPath)
{
    auto check = Database::open(backupPath);
    if (!check.ok())
        return Error{Error::Code::Invalid, QStringLiteral("%1 is not a library Lumen can open: %2")
                                              .arg(QFileInfo(backupPath).fileName(), check.error().message)};
    if (auto intact = check.value()->isIntact(); !intact.ok() || !intact.value())
        return Error{Error::Code::Invalid, QStringLiteral("%1 is damaged").arg(QFileInfo(backupPath).fileName())};
    check.value().reset();
    QFile marker(pendingFile(libraryPath));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return Error{Error::Code::Database, QStringLiteral("Could not record the restore")};
    marker.write(QFileInfo(backupPath).absoluteFilePath().toUtf8());
    return success();
}

Result<bool> Backups::applyPendingRestore(const QString &libraryPath)
{
    QFile marker(pendingFile(libraryPath));
    if (!marker.exists())
        return false;
    if (!marker.open(QIODevice::ReadOnly))
        return Error{Error::Code::Database, QStringLiteral("Could not read the restore request")};
    const QString backup = QString::fromUtf8(marker.readAll()).trimmed();
    marker.close();
    if (!QFileInfo::exists(backup)) {
        marker.remove();
        return Error{Error::Code::NotFound, QStringLiteral("The backup %1 is gone; nothing was restored").arg(backup)};
    }
    // Keep the current library (with its log files) beside it.
    const QFileInfo library(libraryPath);
    const QString kept = library.dir().filePath(
        QStringLiteral("library-before-restore-%1.db").arg(QDateTime::currentDateTimeUtc().toString(timeFormat)));
    const QStringList from = databaseFiles(libraryPath);
    const QStringList to = databaseFiles(kept);
    for (int i = 0; i < from.size(); ++i) {
        if (QFileInfo::exists(from[i]) && !QFile::rename(from[i], to[i]))
            return Error{Error::Code::Database, QStringLiteral("Could not move the current library aside")};
    }
    if (!QFile::copy(backup, libraryPath)) {
        // Put the library back as it was.
        for (int i = 0; i < from.size(); ++i)
            QFile::rename(to[i], from[i]);
        return Error{Error::Code::Database, QStringLiteral("Could not copy the backup into place")};
    }
    QFile::setPermissions(libraryPath, QFile::ReadOwner | QFile::WriteOwner);
    marker.remove();
    return true;
}

} // namespace lumen::app
