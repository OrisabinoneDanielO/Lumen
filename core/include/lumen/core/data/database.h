#pragma once

#include "lumen/core/result.h"

#include <QSqlDatabase>
#include <QString>

#include <memory>

namespace lumen::core::data {

// One open SQLite database: the local library index and plans.
//
// Opening turns on foreign keys and WAL journalling, then brings the schema
// up to date. A database written by a newer Lumen is refused rather than
// touched. Use from one thread at a time.
class Database
{
public:
    static QString inMemory() { return QStringLiteral(":memory:"); }

    static Result<std::unique_ptr<Database>> open(const QString &path);

    // Highest schema version this build knows how to create.
    static int latestSchemaVersion();

    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    QString path() const { return m_path; }
    int schemaVersion() const;

    // The Qt connection, for repositories and tests.
    QSqlDatabase connection() const;

private:
    Database(QString connectionName, QString path);
    Status configure();
    Status migrate();

    QString m_connectionName;
    QString m_path;
};

} // namespace lumen::core::data
