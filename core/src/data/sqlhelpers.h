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

// Runs work inside a transaction, rolling back if it fails.
template <typename Work>
Status inTransaction(QSqlDatabase db, Work &&work)
{
    if (!db.transaction())
        return sqlError(db.lastError(), QStringLiteral("begin transaction"));
    Status status = work();
    if (!status.ok()) {
        db.rollback();
        return status;
    }
    if (!db.commit()) {
        Error error = sqlError(db.lastError(), QStringLiteral("commit"));
        db.rollback();
        return error;
    }
    return status;
}

} // namespace lumen::core::data::detail
