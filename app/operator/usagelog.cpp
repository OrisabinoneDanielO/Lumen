#include "usagelog.h"

#include "lumen/core/data/repository.h"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

namespace lumen::app {

using namespace core;
using namespace core::data;

namespace {

Error sqlError(const QSqlQuery &query, const QString &what)
{
    return Error{Error::Code::Database, QStringLiteral("%1: %2").arg(what, query.lastError().text())};
}

QString csvField(const QString &value)
{
    QString v = value;
    // A leading = + - @ would make a spreadsheet treat it as a formula.
    if (!v.isEmpty() && QStringLiteral("=+-@").contains(v.front()))
        v.prepend(u'\'');
    if (v.contains(u',') || v.contains(u'"') || v.contains(u'\n'))
        v = u'"' + v.replace(QStringLiteral("\""), QStringLiteral("\"\"")) + u'"';
    return v;
}

} // namespace

Status UsageLog::record(const QString &presentationId, QDate day)
{
    const QString key = presentationId + u'|' + day.toString(Qt::ISODate);
    if (presentationId.isEmpty() || m_recorded.contains(key))
        return success();
    auto presentation = Repository<Presentation>(m_db).get(presentationId);
    if (!presentation.ok())
        return presentation.error();
    const QJsonObject &meta = presentation.value().metadata;
    QSqlQuery query(m_db.connection());
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO song_usage "
                                 "(presentation_id, shown_on, title, author, ccli, copyright) VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(presentationId);
    query.addBindValue(day.toString(Qt::ISODate));
    // Never NULL: OR IGNORE would drop the row without a word.
    const auto text = [](const QString &value) { return value.isNull() ? QStringLiteral("") : value; };
    query.addBindValue(text(presentation.value().title));
    query.addBindValue(text(meta.value(QStringLiteral("author")).toString()));
    query.addBindValue(text(meta.value(QStringLiteral("ccliNumber")).toString()));
    query.addBindValue(text(meta.value(QStringLiteral("copyright")).toString()));
    if (!query.exec())
        return sqlError(query, QStringLiteral("Recording song usage"));
    m_recorded.insert(key);
    return success();
}

Result<std::vector<UsageLog::Row>> UsageLog::report(QDate from, QDate to) const
{
    QSqlQuery query(m_db.connection());
    // Grouped by song; the newest details (title, CCLI number) are used.
    query.prepare(QStringLiteral(
        "SELECT presentation_id, COUNT(*), MIN(shown_on), MAX(shown_on), "
        "(SELECT title FROM song_usage l WHERE l.presentation_id = u.presentation_id ORDER BY shown_on DESC LIMIT 1), "
        "(SELECT author FROM song_usage l WHERE l.presentation_id = u.presentation_id ORDER BY shown_on DESC LIMIT 1), "
        "(SELECT ccli FROM song_usage l WHERE l.presentation_id = u.presentation_id ORDER BY shown_on DESC LIMIT 1), "
        "(SELECT copyright FROM song_usage l WHERE l.presentation_id = u.presentation_id ORDER BY shown_on DESC LIMIT 1) "
        "FROM song_usage u WHERE shown_on BETWEEN ? AND ? "
        "GROUP BY presentation_id ORDER BY COUNT(*) DESC, 5 COLLATE NOCASE"));
    query.addBindValue(from.toString(Qt::ISODate));
    query.addBindValue(to.toString(Qt::ISODate));
    if (!query.exec())
        return sqlError(query, QStringLiteral("Reading song usage"));
    std::vector<Row> rows;
    while (query.next()) {
        rows.push_back({query.value(4).toString(), query.value(5).toString(), query.value(6).toString(),
                        query.value(7).toString(), query.value(1).toInt(),
                        QDate::fromString(query.value(2).toString(), Qt::ISODate),
                        QDate::fromString(query.value(3).toString(), Qt::ISODate)});
    }
    return rows;
}

Status UsageLog::exportCsv(const QString &file, QDate from, QDate to) const
{
    auto rows = report(from, to);
    if (!rows.ok())
        return rows.error();
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return Error{Error::Code::Database, QStringLiteral("%1 could not be written: %2").arg(file, out.errorString())};
    QTextStream stream(&out);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "\xEF\xBB\xBF"; // so spreadsheets read accents correctly
    stream << "Title,Author,CCLI song number,Copyright,Times used,First used,Last used\r\n";
    for (const Row &row : rows.value()) {
        stream << csvField(row.title) << u',' << csvField(row.author) << u',' << csvField(row.ccli) << u','
               << csvField(row.copyright) << u',' << row.uses << u',' << row.first.toString(Qt::ISODate) << u','
               << row.last.toString(Qt::ISODate) << "\r\n";
    }
    stream.flush();
    if (out.error() != QFileDevice::NoError)
        return Error{Error::Code::Database, QStringLiteral("%1 could not be written: %2").arg(file, out.errorString())};
    return success();
}

} // namespace lumen::app
