#pragma once

#include "lumen/core/data/database.h"
#include "lumen/core/data/entities.h"
#include "lumen/core/result.h"

#include <QDate>
#include <QString>

#include <vector>

namespace lumen::app {

// Saving, reusing and reordering service plans (batch 11). Every change is
// written to the library at once, inside a transaction, so nothing is lost
// if the app stops.
namespace plans {

struct PlanSummary
{
    QString id;
    QString name;
    QDate date;
    int items = 0;
};

// Newest date first, then by name.
core::Result<std::vector<PlanSummary>> list(core::data::Database &db);

core::Result<core::data::Plan> create(core::data::Database &db, const QString &name, const QDate &date);

// A copy with every item, header and note, named `name`, dated `date`.
core::Result<core::data::Plan> duplicate(core::data::Database &db, const QString &planId, const QString &name,
                                         const QDate &date);

core::Status remove(core::data::Database &db, const QString &planId);
core::Status rename(core::data::Database &db, const QString &planId, const QString &name, const QDate &date);

// Inserts a header (a label such as "Worship" or "Sermon") at `position`
// (clamped to the end).
core::Result<core::data::PlanItem> addHeader(core::data::Database &db, const QString &planId, int position,
                                             const QString &text);

// Removes one item; the items after it move up.
core::Status removeItem(core::data::Database &db, const QString &planId, int position);

// Moves the item at `from` to `to`; the others keep their order.
core::Status moveItem(core::data::Database &db, const QString &planId, int from, int to);

core::Status setNotes(core::data::Database &db, const QString &planId, int position, const QString &notes);

} // namespace plans
} // namespace lumen::app
