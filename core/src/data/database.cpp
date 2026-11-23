#include "lumen/core/data/database.h"

#include "sqlhelpers.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QUuid>

#include <vector>

namespace lumen::core::data {

namespace {

constexpr auto driverName = "QSQLITE";

struct Migration
{
    int version;
    QString path;
};

// Migration files built into the library as resources, sorted by version.
const std::vector<Migration> &migrations()
{
    static const std::vector<Migration> list = [] {
        std::vector<Migration> found;
        static const QRegularExpression pattern(QStringLiteral(R"(^(\d{4})_[a-z0-9_]+\.sql$)"));
        const QDir dir(QStringLiteral(":/lumen/migrations"));
        for (const QString &name : dir.entryList(QDir::Files, QDir::Name)) {
            const auto match = pattern.match(name);
            if (match.hasMatch())
                found.push_back({match.captured(1).toInt(), dir.filePath(name)});
        }
        return found;
    }();
    return list;
}

// Splits a migration script into statements on semicolons that are outside
// quotes and comments. Comments are dropped.
QStringList splitStatements(const QString &sql)
{
    QStringList statements;
    QString current;
    QChar quote;
    for (qsizetype i = 0; i < sql.size(); ++i) {
        const QChar c = sql.at(i);
        if (!quote.isNull()) {
            current += c;
            if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == u'-' && i + 1 < sql.size() && sql.at(i + 1) == u'-') {
            while (i < sql.size() && sql.at(i) != u'\n')
                ++i;
            current += u'\n';
            continue;
        }
        if (c == u'\'' || c == u'"') {
            quote = c;
            current += c;
        } else if (c == u';') {
            if (!current.trimmed().isEmpty())
                statements << current.trimmed();
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.trimmed().isEmpty())
        statements << current.trimmed();
    return statements;
}

Result<QVariant> pragma(const QSqlDatabase &db, const QString &statement)
{
    QSqlQuery query(db);
    if (!query.exec(statement))
        return detail::sqlError(query.lastError(), statement);
    return query.next() ? query.value(0) : QVariant();
}

} // namespace

Database::Database(QString connectionName, QString path)
    : m_connectionName(std::move(connectionName)), m_path(std::move(path))
{}

Database::~Database()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

int Database::latestSchemaVersion()
{
    const auto &all = migrations();
    return all.empty() ? 0 : all.back().version;
}

QSqlDatabase Database::connection() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

int Database::schemaVersion() const
{
    const auto version = pragma(connection(), QStringLiteral("PRAGMA user_version"));
    return version.ok() ? version.value().toInt() : -1;
}

Result<std::unique_ptr<Database>> Database::open(const QString &path)
{
    if (!QSqlDatabase::isDriverAvailable(QLatin1String(driverName)))
        return Error{Error::Code::Database, QStringLiteral("The Qt SQLite driver is not available")};

    const QString name = QStringLiteral("lumen-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::unique_ptr<Database> database(new Database(name, path));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QLatin1String(driverName), name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
        if (!db.open())
            return detail::sqlError(db.lastError(), QStringLiteral("Opening ") + path);
    }

    if (auto status = database->configure(); !status.ok())
        return status.error();

    const int version = database->schemaVersion();
    if (version > latestSchemaVersion()) {
        return Error{Error::Code::SchemaTooNew,
                     QStringLiteral("%1 uses schema version %2, but this version of Lumen only "
                                    "understands up to %3. Update Lumen to open it.")
                         .arg(path)
                         .arg(version)
                         .arg(latestSchemaVersion())};
    }

    if (auto status = database->migrate(); !status.ok())
        return status.error();

    return Result<std::unique_ptr<Database>>(std::move(database));
}

Status Database::configure()
{
    const QSqlDatabase db = connection();
    // Neither pragma can run inside a transaction, so set both before migrating.
    for (const char *statement : {"PRAGMA foreign_keys = ON", "PRAGMA journal_mode = WAL"}) {
        if (auto result = pragma(db, QLatin1String(statement)); !result.ok())
            return result.error();
    }
    const auto foreignKeys = pragma(db, QStringLiteral("PRAGMA foreign_keys"));
    if (!foreignKeys.ok() || foreignKeys.value().toInt() != 1) {
        return Error{Error::Code::Database,
                     QStringLiteral("This SQLite build cannot enforce foreign keys")};
    }
    return success();
}

Status Database::migrate()
{
    const auto &all = migrations();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].version != int(i) + 1) {
            return Error{Error::Code::Migration,
                         QStringLiteral("Migrations must be numbered 0001, 0002, ... without gaps")};
        }
    }

    QSqlDatabase db = connection();
    const int current = schemaVersion();
    for (const Migration &migration : all) {
        if (migration.version <= current)
            continue;

        QFile file(migration.path);
        if (!file.open(QIODevice::ReadOnly)) {
            return Error{Error::Code::Migration,
                         QStringLiteral("Cannot read migration ") + migration.path};
        }
        const QStringList statements = splitStatements(QString::fromUtf8(file.readAll()));

        const Status status = detail::inTransaction(db, [&]() -> Status {
            for (const QString &statement : statements) {
                QSqlQuery query(db);
                if (!query.exec(statement))
                    return detail::sqlError(query.lastError(),
                                            QStringLiteral("Migration %1").arg(migration.version));
            }
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("PRAGMA user_version = %1").arg(migration.version)))
                return detail::sqlError(query.lastError(), QStringLiteral("Setting schema version"));
            return success();
        });
        if (!status.ok())
            return Error{Error::Code::Migration, status.error().message};
    }
    return success();
}

} // namespace lumen::core::data

namespace lumen::core::data {

Status Database::inTransaction(const std::function<Status()> &work)
{
    return detail::inTransaction(connection(), work);
}

} // namespace lumen::core::data
