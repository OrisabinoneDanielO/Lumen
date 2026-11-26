#pragma once

#include "lumen/core/result.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantList>

namespace lumen::core::data::detail {

// Maps a Qt SQL error to an Error. SQLite reports every constraint failure
// (foreign key, CHECK, UNIQUE, NOT NULL) with primary result code 19.
inline Error sqlError(const QSqlError &error, const QString &context)
{
    constexpr int sqliteConstraint = 19;
    bool isNumber = false;
    const int code = error.nativeErrorCode().toInt(&isNumber);
    const bool constraint = (isNumber && (code & 0xFF) == sqliteConstraint)
                            || error.databaseText().contains(QLatin1String("constraint"),
                                                             Qt::CaseInsensitive);
    return Error{constraint ? Error::Code::Constraint : Error::Code::Database,
                 context + QLatin1String(": ") + error.text()};
}

// Qt SQL binds a null QString as SQL NULL. Entity text fields use QString()
// to mean "empty", so bind those as ''. Values meant to be NULL (optional
// references, dates) are null QVariants and pass through unchanged.
inline QVariant bindable(const QVariant &value)
{
    if (!value.isNull() && value.typeId() == QMetaType::QString && value.toString().isNull())
        return QVariant(QStringLiteral(""));
    return value;
}

// Prepares sql, binds the values in order and runs it.
inline Status exec(QSqlQuery &query, const QString &sql, const QVariantList &values,
                   const QString &context)
{
    if (!query.prepare(sql))
        return sqlError(query.lastError(), context);
    for (qsizetype i = 0; i < values.size(); ++i)
        query.bindValue(int(i), bindable(values.at(i)));
    if (!query.exec())
        return sqlError(query.lastError(), context);
    return success();
}

// Runs work all-or-nothing, rolling back if it fails. Uses SQLite savepoints,
// which nest: work may itself call inTransaction (e.g. a song import saving an
// arrangement). Outside any transaction the outermost savepoint acts as one.
template <typename Work>
Status inTransaction(QSqlDatabase db, Work &&work)
{
    static thread_local int depth = 0;
    const QString name = QStringLiteral("lumen_%1").arg(depth);

    QSqlQuery begin(db);
    if (!begin.exec(QStringLiteral("SAVEPOINT ") + name))
        return sqlError(begin.lastError(), QStringLiteral("begin transaction"));

    ++depth;
    Status status = work();
    --depth;

    QSqlQuery end(db);
    if (!status.ok()) {
        end.exec(QStringLiteral("ROLLBACK TO ") + name);
        end.exec(QStringLiteral("RELEASE ") + name);
        return status;
    }
    if (!end.exec(QStringLiteral("RELEASE ") + name)) {
        Error error = sqlError(end.lastError(), QStringLiteral("commit"));
        QSqlQuery undo(db);
        undo.exec(QStringLiteral("ROLLBACK TO ") + name);
        undo.exec(QStringLiteral("RELEASE ") + name);
        return error;
    }
    return status;
}

} // namespace lumen::core::data::detail
