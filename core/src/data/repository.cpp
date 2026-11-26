#include "lumen/core/data/repository.h"

#include "sqlhelpers.h"

#include <QJsonDocument>
#include <QUuid>

#include <array>
#include <concepts>
#include <utility>

namespace lumen::core::data {

namespace {

using detail::exec;
using detail::sqlError;

// ---- value conversions --------------------------------------------------

QVariant nullString()
{
    return QVariant(QMetaType::fromType<QString>());
}

// Empty reference ids are stored as NULL.
QVariant idOrNull(const QString &id)
{
    return id.isEmpty() ? nullString() : QVariant(id);
}

QString idFrom(const QVariant &value)
{
    return value.isNull() ? QString() : value.toString();
}

QString jsonText(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString jsonText(const QJsonArray &array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QJsonObject objectFrom(const QVariant &value)
{
    return QJsonDocument::fromJson(value.toString().toUtf8()).object();
}

QJsonArray arrayFrom(const QVariant &value)
{
    return QJsonDocument::fromJson(value.toString().toUtf8()).array();
}

QString rectText(const QRectF &rect)
{
    return jsonText(QJsonObject{{QStringLiteral("x"), rect.x()},
                                {QStringLiteral("y"), rect.y()},
                                {QStringLiteral("w"), rect.width()},
                                {QStringLiteral("h"), rect.height()}});
}

QRectF rectFrom(const QVariant &value)
{
    const QJsonObject o = objectFrom(value);
    return {o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble(),
            o.value(QStringLiteral("w")).toDouble(), o.value(QStringLiteral("h")).toDouble()};
}

QVariant dateTimeOrNull(const std::optional<QDateTime> &value)
{
    return value ? QVariant(value->toUTC().toString(Qt::ISODateWithMs)) : nullString();
}

std::optional<QDateTime> dateTimeFrom(const QVariant &value)
{
    if (value.isNull())
        return std::nullopt;
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}

QVariant dateOrNull(const QDate &date)
{
    return date.isValid() ? QVariant(date.toString(Qt::ISODate)) : nullString();
}

QDate dateFrom(const QVariant &value)
{
    return value.isNull() ? QDate() : QDate::fromString(value.toString(), Qt::ISODate);
}

QVariant timeOrNull(const std::optional<QTime> &time)
{
    return time ? QVariant(time->toString(Qt::ISODateWithMs)) : nullString();
}

std::optional<QTime> timeFrom(const QVariant &value)
{
    if (value.isNull())
        return std::nullopt;
    return QTime::fromString(value.toString(), Qt::ISODateWithMs);
}

// ---- enums are stored as stable lower-case names ------------------------

template <typename E>
struct EnumNames;

template <>
struct EnumNames<PresentationKind>
{
    static constexpr std::array<std::pair<PresentationKind, const char *>, 4> items{{
        {PresentationKind::Song, "song"},
        {PresentationKind::Scripture, "scripture"},
        {PresentationKind::Slides, "slides"},
        {PresentationKind::Media, "media"},
    }};
};

template <>
struct EnumNames<ElementType>
{
    static constexpr std::array<std::pair<ElementType, const char *>, 6> items{{
        {ElementType::Text, "text"},
        {ElementType::Image, "image"},
        {ElementType::Video, "video"},
        {ElementType::Shape, "shape"},
        {ElementType::Timer, "timer"},
        {ElementType::Clock, "clock"},
    }};
};

template <>
struct EnumNames<ThemeTarget>
{
    static constexpr std::array<std::pair<ThemeTarget, const char *>, 6> items{{
        {ThemeTarget::Any, "any"},
        {ThemeTarget::Song, "song"},
        {ThemeTarget::Scripture, "scripture"},
        {ThemeTarget::Slides, "slides"},
        {ThemeTarget::Media, "media"},
        {ThemeTarget::LowerThird, "lower_third"},
    }};
};

template <>
struct EnumNames<OutputLayout>
{
    static constexpr std::array<std::pair<OutputLayout, const char *>, 3> items{{
        {OutputLayout::Audience, "audience"},
        {OutputLayout::Stage, "stage"},
        {OutputLayout::KeyFill, "key_fill"},
    }};
};

template <>
struct EnumNames<TimerMode>
{
    static constexpr std::array<std::pair<TimerMode, const char *>, 3> items{{
        {TimerMode::Countdown, "countdown"},
        {TimerMode::CountdownToTime, "countdown_to_time"},
        {TimerMode::Elapsed, "elapsed"},
    }};
};

template <typename E>
QString enumText(E value)
{
    for (const auto &[item, name] : EnumNames<E>::items) {
        if (item == value)
            return QString::fromLatin1(name);
    }
    return {};
}

template <typename E>
E enumFrom(const QVariant &value, E fallback)
{
    const QString text = value.toString();
    for (const auto &[item, name] : EnumNames<E>::items) {
        if (text == QLatin1String(name))
            return item;
    }
    return fallback;
}

// ---- table mappings -----------------------------------------------------
//
// Each Table<T> gives the table name, its columns other than id, how to turn
// an entity into column values (same order) and back from a row whose first
// column is id. parentWhere filters listChildren (nullptr for root entities).

template <typename T>
struct Table;

template <>
struct Table<Library>
{
    static constexpr const char *name = "libraries";
    static constexpr const char *parentWhere = nullptr;
    static constexpr const char *orderBy = "name, id";
    static QStringList columns() { return {"name", "folder_path", "last_backup"}; }
    static QVariantList values(const Library &e)
    {
        return {e.name, e.folderPath, dateTimeOrNull(e.lastBackup)};
    }
    static Library read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                dateTimeFrom(q.value(3))};
    }
};

template <>
struct Table<Presentation>
{
    static constexpr const char *name = "presentations";
    static constexpr const char *parentWhere = "library_id = :parent";
    static constexpr const char *orderBy = "title, id";
    static QStringList columns() { return {"library_id", "kind", "title", "theme_id", "metadata"}; }
    static QVariantList values(const Presentation &e)
    {
        return {e.libraryId, enumText(e.kind), e.title, idOrNull(e.themeId), jsonText(e.metadata)};
    }
    static Presentation read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(),
                enumFrom(q.value(2), PresentationKind::Slides), q.value(3).toString(),
                idFrom(q.value(4)), objectFrom(q.value(5))};
    }
};

template <>
struct Table<Group>
{
    static constexpr const char *name = "slide_groups";
    static constexpr const char *parentWhere = "presentation_id = :parent";
    static constexpr const char *orderBy = "presentation_id, position, id";
    static QStringList columns()
    {
        return {"presentation_id", "position", "label", "colour", "hotkey"};
    }
    static QVariantList values(const Group &e)
    {
        return {e.presentationId, e.position, e.label, e.colour, e.hotkey};
    }
    static Group read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(), q.value(2).toInt(),
                q.value(3).toString(), q.value(4).toString(), q.value(5).toString()};
    }
};

template <>
struct Table<Arrangement>
{
    static constexpr const char *name = "arrangements";
    static constexpr const char *parentWhere = "presentation_id = :parent";
    static constexpr const char *orderBy = "presentation_id, name, id";
    static QStringList columns() { return {"presentation_id", "name", "is_default"}; }
    static QVariantList values(const Arrangement &e)
    {
        return {e.presentationId, e.name, e.isDefault ? 1 : 0};
    }
    static Arrangement read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                q.value(3).toInt() != 0, {}};
    }

    // The play order lives in arrangement_groups.
    static Status writeChildren(QSqlDatabase db, const Arrangement &e)
    {
        QSqlQuery clear(db);
        if (auto s = exec(clear, QStringLiteral("DELETE FROM arrangement_groups WHERE arrangement_id = ?"),
                          {e.id}, QStringLiteral("Clearing arrangement"));
            !s.ok())
            return s;
        QSqlQuery insert(db);
        if (!insert.prepare(QStringLiteral("INSERT INTO arrangement_groups (arrangement_id, position, "
                                           "group_id) VALUES (?, ?, ?)")))
            return sqlError(insert.lastError(), QStringLiteral("Saving arrangement"));
        for (qsizetype i = 0; i < e.groupIds.size(); ++i) {
            insert.bindValue(0, e.id);
            insert.bindValue(1, int(i));
            insert.bindValue(2, e.groupIds.at(i));
            if (!insert.exec())
                return sqlError(insert.lastError(), QStringLiteral("Saving arrangement"));
        }
        return success();
    }

    static Status readChildren(QSqlDatabase db, Arrangement &e)
    {
        QSqlQuery q(db);
        if (auto s = exec(q, QStringLiteral("SELECT group_id FROM arrangement_groups "
                                            "WHERE arrangement_id = ? ORDER BY position"),
                          {e.id}, QStringLiteral("Reading arrangement"));
            !s.ok())
            return s;
        e.groupIds.clear();
        while (q.next())
            e.groupIds << q.value(0).toString();
        return success();
    }
};

template <>
struct Table<Slide>
{
    static constexpr const char *name = "slides";
    static constexpr const char *parentWhere = "group_id = :parent";
    static constexpr const char *orderBy = "group_id, position, id";
    static QStringList columns() { return {"group_id", "position", "notes", "chords", "transition"}; }
    static QVariantList values(const Slide &e)
    {
        return {e.groupId, e.position, e.notes, e.chords, jsonText(e.transition)};
    }
    static Slide read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(), q.value(2).toInt(),
                q.value(3).toString(), q.value(4).toString(), objectFrom(q.value(5))};
    }
};

template <>
struct Table<Element>
{
    static constexpr const char *name = "elements";
    // Ids are UUIDs, so one parent id can only match a slide or a theme.
    static constexpr const char *parentWhere = "(slide_id = :parent OR theme_id = :parent)";
    static constexpr const char *orderBy = "z_order, id";
    static QStringList columns()
    {
        return {"slide_id", "theme_id", "type",    "x",       "y",
                "width",    "height",   "style",   "z_order", "content",
                "media_asset_id"};
    }
    static QVariantList values(const Element &e)
    {
        return {idOrNull(e.slideId), idOrNull(e.themeId), enumText(e.type),
                e.frame.x(),         e.frame.y(),         e.frame.width(),
                e.frame.height(),    jsonText(e.style),   e.zOrder,
                e.content,           idOrNull(e.mediaAssetId)};
    }
    static Element read(const QSqlQuery &q)
    {
        return {q.value(0).toString(),
                idFrom(q.value(1)),
                idFrom(q.value(2)),
                enumFrom(q.value(3), ElementType::Text),
                QRectF(q.value(4).toDouble(), q.value(5).toDouble(), q.value(6).toDouble(),
                       q.value(7).toDouble()),
                objectFrom(q.value(8)),
                q.value(9).toInt(),
                q.value(10).toString(),
                idFrom(q.value(11))};
    }
};

template <>
struct Table<Theme>
{
    static constexpr const char *name = "themes";
    static constexpr const char *parentWhere = nullptr;
    static constexpr const char *orderBy = "name, id";
    static QStringList columns() { return {"name", "applies_to", "background", "safe_area", "is_default"}; }
    static QVariantList values(const Theme &e)
    {
        return {e.name, enumText(e.appliesTo), jsonText(e.background), rectText(e.safeArea), e.isDefault ? 1 : 0};
    }
    static Theme read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(), enumFrom(q.value(2), ThemeTarget::Any),
                objectFrom(q.value(3)), rectFrom(q.value(4)), q.value(5).toInt() != 0};
    }
};

template <>
struct Table<MediaAsset>
{
    static constexpr const char *name = "media_assets";
    static constexpr const char *parentWhere = "library_id = :parent";
    static constexpr const char *orderBy = "file_path, id";
    static QStringList columns()
    {
        return {"library_id",     "file_path", "hash",       "duration_ms",
                "thumbnail_path", "loop",      "trim_in_ms", "trim_out_ms"};
    }
    static QVariantList values(const MediaAsset &e)
    {
        return {e.libraryId,     e.filePath,    e.hash,     e.durationMs,
                e.thumbnailPath, e.loop ? 1 : 0, e.trimInMs, e.trimOutMs};
    }
    static MediaAsset read(const QSqlQuery &q)
    {
        return {q.value(0).toString(),   q.value(1).toString(),   q.value(2).toString(),
                q.value(3).toString(),   q.value(4).toLongLong(), q.value(5).toString(),
                q.value(6).toInt() != 0, q.value(7).toLongLong(), q.value(8).toLongLong()};
    }
};

template <>
struct Table<Plan>
{
    static constexpr const char *name = "plans";
    static constexpr const char *parentWhere = nullptr;
    static constexpr const char *orderBy = "date, name, id";
    static QStringList columns() { return {"name", "date"}; }
    static QVariantList values(const Plan &e) { return {e.name, dateOrNull(e.date)}; }
    static Plan read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(), dateFrom(q.value(2))};
    }
};

template <>
struct Table<PlanItem>
{
    static constexpr const char *name = "plan_items";
    static constexpr const char *parentWhere = "plan_id = :parent";
    static constexpr const char *orderBy = "plan_id, position, id";
    static QStringList columns()
    {
        return {"plan_id", "position", "presentation_id", "arrangement_id", "header_text", "notes"};
    }
    static QVariantList values(const PlanItem &e)
    {
        return {e.planId, e.position, idOrNull(e.presentationId), idOrNull(e.arrangementId),
                e.headerText, e.notes};
    }
    static PlanItem read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(), q.value(2).toInt(),
                idFrom(q.value(3)),    idFrom(q.value(4)),    q.value(5).toString(),
                q.value(6).toString()};
    }
};

template <>
struct Table<Output>
{
    static constexpr const char *name = "outputs";
    static constexpr const char *parentWhere = nullptr;
    static constexpr const char *orderBy = "name, id";
    static QStringList columns()
    {
        return {"name",   "target", "width",  "height",
                "frame_rate", "layout", "layers", "stage_layout_id"};
    }
    static QVariantList values(const Output &e)
    {
        return {e.name,      e.target,           e.width,          e.height,
                e.frameRate, enumText(e.layout), jsonText(e.layers), idOrNull(e.stageLayoutId)};
    }
    static Output read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(),
                q.value(2).toString(), q.value(3).toInt(),
                q.value(4).toInt(),    q.value(5).toDouble(),
                enumFrom(q.value(6), OutputLayout::Audience), objectFrom(q.value(7)),
                idFrom(q.value(8))};
    }
};

template <>
struct Table<StageLayout>
{
    static constexpr const char *name = "stage_layouts";
    static constexpr const char *parentWhere = nullptr;
    static constexpr const char *orderBy = "name, id";
    static QStringList columns() { return {"name", "widgets"}; }
    static QVariantList values(const StageLayout &e) { return {e.name, jsonText(e.widgets)}; }
    static StageLayout read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(), arrayFrom(q.value(2))};
    }
};

template <>
struct Table<Message>
{
    static constexpr const char *name = "messages";
    static constexpr const char *parentWhere = nullptr;
    static constexpr const char *orderBy = "name, id";
    static QStringList columns() { return {"name", "template_text", "fields"}; }
    static QVariantList values(const Message &e)
    {
        return {e.name, e.templateText, jsonText(e.fields)};
    }
    static Message read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),
                arrayFrom(q.value(3))};
    }
};

template <>
struct Table<Timer>
{
    static constexpr const char *name = "timers";
    static constexpr const char *parentWhere = nullptr;
    static constexpr const char *orderBy = "name, id";
    static QStringList columns() { return {"name", "mode", "duration_ms", "target_time"}; }
    static QVariantList values(const Timer &e)
    {
        return {e.name, enumText(e.mode), e.durationMs, timeOrNull(e.targetTime)};
    }
    static Timer read(const QSqlQuery &q)
    {
        return {q.value(0).toString(), q.value(1).toString(),
                enumFrom(q.value(2), TimerMode::Countdown), q.value(3).toLongLong(),
                timeFrom(q.value(4))};
    }
};

// Entities with rows in a second table (only Arrangement today).
template <typename T>
concept HasChildRows = requires(QSqlDatabase db, const T &in, T &out) {
    { Table<T>::writeChildren(db, in) } -> std::same_as<Status>;
    { Table<T>::readChildren(db, out) } -> std::same_as<Status>;
};

template <typename T>
QString tableName()
{
    return QLatin1String(Table<T>::name);
}

template <typename T>
QString selectSql()
{
    return QStringLiteral("SELECT id, %1 FROM %2").arg(Table<T>::columns().join(u", "), tableName<T>());
}

template <typename T>
Result<std::vector<T>> readRows(QSqlQuery &query, QSqlDatabase db)
{
    std::vector<T> rows;
    while (query.next())
        rows.push_back(Table<T>::read(query));
    if constexpr (HasChildRows<T>) {
        query.finish();
        for (T &row : rows) {
            if (auto s = Table<T>::readChildren(db, row); !s.ok())
                return s.error();
        }
    }
    return rows;
}

Error notFound(const QString &table, const QString &id)
{
    return Error{Error::Code::NotFound, QStringLiteral("No %1 row with id %2").arg(table, id)};
}

} // namespace

// ---- Repository ---------------------------------------------------------

template <typename T>
Result<T> Repository<T>::create(T entity)
{
    if (entity.id.isEmpty())
        entity.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QSqlDatabase db = m_db.connection();
    const QStringList columns = Table<T>::columns();
    const QString sql = QStringLiteral("INSERT INTO %1 (id, %2) VALUES (?%3)")
                            .arg(tableName<T>(), columns.join(u", "),
                                 QStringLiteral(", ?").repeated(columns.size()));

    auto insert = [&]() -> Status {
        QSqlQuery query(db);
        QVariantList values{entity.id};
        values += Table<T>::values(entity);
        if (auto s = exec(query, sql, values, QStringLiteral("Creating ") + tableName<T>()); !s.ok())
            return s;
        if constexpr (HasChildRows<T>)
            return Table<T>::writeChildren(db, entity);
        return success();
    };

    Status status = HasChildRows<T> ? detail::inTransaction(db, insert) : insert();
    if (!status.ok())
        return status.error();
    return entity;
}

template <typename T>
Result<T> Repository<T>::get(const QString &id) const
{
    QSqlDatabase db = m_db.connection();
    QSqlQuery query(db);
    if (auto s = exec(query, selectSql<T>() + QStringLiteral(" WHERE id = ?"), {id},
                      QStringLiteral("Reading ") + tableName<T>());
        !s.ok())
        return s.error();
    if (!query.next())
        return notFound(tableName<T>(), id);

    T entity = Table<T>::read(query);
    if constexpr (HasChildRows<T>) {
        query.finish();
        if (auto s = Table<T>::readChildren(db, entity); !s.ok())
            return s.error();
    }
    return entity;
}

template <typename T>
Result<std::vector<T>> Repository<T>::list() const
{
    QSqlDatabase db = m_db.connection();
    QSqlQuery query(db);
    query.setForwardOnly(true);
    const QString sql = selectSql<T>() + QStringLiteral(" ORDER BY ") + QLatin1String(Table<T>::orderBy);
    if (auto s = exec(query, sql, {}, QStringLiteral("Listing ") + tableName<T>()); !s.ok())
        return s.error();
    return readRows<T>(query, db);
}

template <typename T>
Result<std::vector<T>> Repository<T>::listChildren(const QString &parentId) const
{
    if constexpr (Table<T>::parentWhere == nullptr) {
        Q_UNUSED(parentId);
        return Error{Error::Code::Invalid, tableName<T>() + QStringLiteral(" rows have no parent")};
    } else {
        QSqlDatabase db = m_db.connection();
        QSqlQuery query(db);
        query.setForwardOnly(true);
        const QString sql = selectSql<T>() + QStringLiteral(" WHERE ")
                            + QLatin1String(Table<T>::parentWhere) + QStringLiteral(" ORDER BY ")
                            + QLatin1String(Table<T>::orderBy);
        if (!query.prepare(sql))
            return sqlError(query.lastError(), QStringLiteral("Listing ") + tableName<T>());
        query.bindValue(QStringLiteral(":parent"), parentId);
        if (!query.exec())
            return sqlError(query.lastError(), QStringLiteral("Listing ") + tableName<T>());
        return readRows<T>(query, db);
    }
}

template <typename T>
Status Repository<T>::update(const T &entity)
{
    QSqlDatabase db = m_db.connection();
    QStringList assignments;
    for (const QString &column : Table<T>::columns())
        assignments << column + QStringLiteral(" = ?");
    const QString sql = QStringLiteral("UPDATE %1 SET %2 WHERE id = ?")
                            .arg(tableName<T>(), assignments.join(u", "));

    auto write = [&]() -> Status {
        QSqlQuery query(db);
        QVariantList values = Table<T>::values(entity);
        values << entity.id;
        if (auto s = exec(query, sql, values, QStringLiteral("Updating ") + tableName<T>()); !s.ok())
            return s;
        if (query.numRowsAffected() == 0)
            return notFound(tableName<T>(), entity.id);
        if constexpr (HasChildRows<T>)
            return Table<T>::writeChildren(db, entity);
        return success();
    };

    return HasChildRows<T> ? detail::inTransaction(db, write) : write();
}

template <typename T>
Status Repository<T>::remove(const QString &id)
{
    QSqlQuery query(m_db.connection());
    if (auto s = exec(query, QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(tableName<T>()), {id},
                      QStringLiteral("Deleting from ") + tableName<T>());
        !s.ok())
        return s;
    if (query.numRowsAffected() == 0)
        return notFound(tableName<T>(), id);
    return success();
}

template class Repository<Library>;
template class Repository<Presentation>;
template class Repository<Group>;
template class Repository<Arrangement>;
template class Repository<Slide>;
template class Repository<Element>;
template class Repository<Theme>;
template class Repository<MediaAsset>;
template class Repository<Plan>;
template class Repository<PlanItem>;
template class Repository<Output>;
template class Repository<StageLayout>;
template class Repository<Message>;
template class Repository<Timer>;

} // namespace lumen::core::data
