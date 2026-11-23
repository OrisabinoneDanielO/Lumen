#include "lumen/core/data/database.h"
#include "lumen/core/data/repository.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace lumen::core;
using namespace lumen::core::data;

namespace {

template <typename T>
QByteArray why(const Result<T> &result)
{
    return result.ok() ? QByteArray() : result.error().message.toUtf8();
}

QVariant scalar(Database &db, const QString &sql)
{
    QSqlQuery query(db.connection());
    if (!query.exec(sql) || !query.next())
        return {};
    return query.value(0);
}

} // namespace

// Opening, configuring and migrating the SQLite database.
class TestDatabase : public QObject
{
    Q_OBJECT

private slots:
    void opensInMemoryAtLatestSchema()
    {
        auto db = Database::open(Database::inMemory());
        QVERIFY2(db.ok(), why(db));
        QVERIFY(Database::latestSchemaVersion() >= 1);
        QCOMPARE(db.value()->schemaVersion(), Database::latestSchemaVersion());
    }

    void enablesForeignKeysAndWal()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto db = Database::open(dir.filePath(QStringLiteral("library.db")));
        QVERIFY2(db.ok(), why(db));
        QCOMPARE(scalar(*db.value(), QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);
        QCOMPARE(scalar(*db.value(), QStringLiteral("PRAGMA journal_mode")).toString(),
                 QStringLiteral("wal"));
    }

    void createsEveryTable()
    {
        auto db = Database::open(Database::inMemory());
        QVERIFY2(db.ok(), why(db));
        QStringList tables;
        QSqlQuery query(db.value()->connection());
        QVERIFY(query.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name")));
        while (query.next())
            tables << query.value(0).toString();
        const QStringList expected{
            "arrangement_groups", "arrangements", "elements", "libraries",
            "media_assets",       "messages",     "outputs",  "plan_items",
            "plans",              "presentations", "slide_groups", "slides",
            "stage_layouts",      "themes",       "timers"};
        QCOMPARE(tables, expected);
    }

    void reopeningKeepsDataAndVersion()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("library.db"));
        QString id;
        {
            auto db = Database::open(path);
            QVERIFY2(db.ok(), why(db));
            auto library = Repository<Library>(*db.value()).create({.name = "Main library"});
            QVERIFY2(library.ok(), why(library));
            id = library.value().id;
        }
        auto db = Database::open(path);
        QVERIFY2(db.ok(), why(db));
        QCOMPARE(db.value()->schemaVersion(), Database::latestSchemaVersion());
        auto library = Repository<Library>(*db.value()).get(id);
        QVERIFY2(library.ok(), why(library));
        QCOMPARE(library.value().name, QStringLiteral("Main library"));
    }

    // An older Lumen must not touch a library written by a newer one.
    void refusesNewerSchema()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("library.db"));
        const int future = Database::latestSchemaVersion() + 1;
        {
            auto db = Database::open(path);
            QVERIFY2(db.ok(), why(db));
            QSqlQuery query(db.value()->connection());
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = %1").arg(future)));
        }
        auto db = Database::open(path);
        QVERIFY(!db.ok());
        QCOMPARE(db.error().code, Error::Code::SchemaTooNew);
        QVERIFY(db.error().message.contains(QString::number(future)));
    }

    void connectionsAreIndependent()
    {
        auto first = Database::open(Database::inMemory());
        auto second = Database::open(Database::inMemory());
        QVERIFY(first.ok() && second.ok());
        QVERIFY(Repository<Library>(*first.value()).create({.name = "Only in first"}).ok());
        auto inSecond = Repository<Library>(*second.value()).list();
        QVERIFY2(inSecond.ok(), why(inSecond));
        QVERIFY(inSecond.value().empty());
    }

    void reportsUnopenablePath()
    {
        QTemporaryDir dir;
        // A directory cannot be opened as a database file.
        auto db = Database::open(dir.path());
        QVERIFY(!db.ok());
        QVERIFY(!db.error().message.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
