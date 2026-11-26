#include "planstore.h"

#include "lumen/core/data/repository.h"

#include <algorithm>

namespace lumen::app::plans {

using namespace core;
using namespace core::data;

namespace {

Error invalid(const QString &message)
{
    return Error{Error::Code::Invalid, message};
}

// Writes positions 0, 1, 2, ... in the given order.
Status renumber(Database &db, std::vector<PlanItem> &items)
{
    for (int i = 0; i < int(items.size()); ++i) {
        if (items[size_t(i)].position == i)
            continue;
        items[size_t(i)].position = i;
        if (auto updated = Repository<PlanItem>(db).update(items[size_t(i)]); !updated.ok())
            return updated;
    }
    return success();
}

} // namespace

Result<std::vector<PlanSummary>> list(Database &db)
{
    auto all = Repository<Plan>(db).list();
    if (!all.ok())
        return all.error();
    std::vector<PlanSummary> rows;
    for (const Plan &plan : all.value()) {
        auto items = Repository<PlanItem>(db).listChildren(plan.id);
        rows.push_back({plan.id, plan.name, plan.date, items.ok() ? int(items.value().size()) : 0});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const PlanSummary &a, const PlanSummary &b) {
        if (a.date != b.date)
            return a.date > b.date; // newest first; undated last
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return rows;
}

Result<Plan> create(Database &db, const QString &name, const QDate &date)
{
    if (name.trimmed().isEmpty())
        return invalid(QStringLiteral("A plan needs a name"));
    return Repository<Plan>(db).create({.name = name.trimmed(), .date = date});
}

Result<Plan> duplicate(Database &db, const QString &planId, const QString &name, const QDate &date)
{
    Plan copy;
    const Status done = db.inTransaction([&]() -> Status {
        auto items = Repository<PlanItem>(db).listChildren(planId);
        if (!items.ok())
            return items.error();
        auto created = create(db, name, date);
        if (!created.ok())
            return created.error();
        copy = created.value();
        for (PlanItem item : items.value()) {
            item.id.clear();
            item.planId = copy.id;
            if (auto added = Repository<PlanItem>(db).create(item); !added.ok())
                return added.error();
        }
        return success();
    });
    if (!done.ok())
        return done.error();
    return copy;
}

Status remove(Database &db, const QString &planId)
{
    return Repository<Plan>(db).remove(planId);
}

Status rename(Database &db, const QString &planId, const QString &name, const QDate &date)
{
    auto plan = Repository<Plan>(db).get(planId);
    if (!plan.ok())
        return plan.error();
    if (name.trimmed().isEmpty())
        return invalid(QStringLiteral("A plan needs a name"));
    plan.value().name = name.trimmed();
    plan.value().date = date;
    return Repository<Plan>(db).update(plan.value());
}

Result<PlanItem> addHeader(Database &db, const QString &planId, int position, const QString &text)
{
    if (text.trimmed().isEmpty())
        return invalid(QStringLiteral("A header needs a label"));
    PlanItem header;
    const Status done = db.inTransaction([&]() -> Status {
        auto items = Repository<PlanItem>(db).listChildren(planId);
        if (!items.ok())
            return items.error();
        position = std::clamp(position, 0, int(items.value().size()));
        auto created = Repository<PlanItem>(db).create(
            {.planId = planId, .position = int(items.value().size()), .headerText = text.trimmed()});
        if (!created.ok())
            return created.error();
        header = created.value();
        std::vector<PlanItem> ordered = items.value();
        ordered.insert(ordered.begin() + position, header);
        if (auto numbered = renumber(db, ordered); !numbered.ok())
            return numbered;
        header.position = position;
        return success();
    });
    if (!done.ok())
        return done.error();
    return header;
}

Status removeItem(Database &db, const QString &planId, int position)
{
    return db.inTransaction([&]() -> Status {
        auto items = Repository<PlanItem>(db).listChildren(planId);
        if (!items.ok())
            return items.error();
        std::vector<PlanItem> ordered = items.value();
        if (position < 0 || position >= int(ordered.size()))
            return invalid(QStringLiteral("No item at position %1").arg(position));
        if (auto removed = Repository<PlanItem>(db).remove(ordered[size_t(position)].id); !removed.ok())
            return removed;
        ordered.erase(ordered.begin() + position);
        return renumber(db, ordered);
    });
}

Status moveItem(Database &db, const QString &planId, int from, int to)
{
    return db.inTransaction([&]() -> Status {
        auto items = Repository<PlanItem>(db).listChildren(planId);
        if (!items.ok())
            return items.error();
        std::vector<PlanItem> ordered = items.value();
        const int count = int(ordered.size());
        if (from < 0 || from >= count || to < 0 || to >= count)
            return invalid(QStringLiteral("Cannot move item %1 to %2").arg(from).arg(to));
        PlanItem moving = ordered[size_t(from)];
        ordered.erase(ordered.begin() + from);
        ordered.insert(ordered.begin() + to, moving);
        return renumber(db, ordered);
    });
}

Status setNotes(Database &db, const QString &planId, int position, const QString &notes)
{
    auto items = Repository<PlanItem>(db).listChildren(planId);
    if (!items.ok())
        return items.error();
    if (position < 0 || position >= int(items.value().size()))
        return invalid(QStringLiteral("No item at position %1").arg(position));
    PlanItem item = items.value()[size_t(position)];
    item.notes = notes;
    return Repository<PlanItem>(db).update(item);
}

} // namespace lumen::app::plans
