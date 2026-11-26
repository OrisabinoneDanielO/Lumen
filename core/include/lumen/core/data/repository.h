#pragma once

#include "lumen/core/data/database.h"
#include "lumen/core/data/entities.h"
#include "lumen/core/result.h"

#include <vector>

namespace lumen::core::data {

// Create, read, update and delete for one entity type.
//
//   Repository<Presentation> songs(db);
//   auto created = songs.create({.libraryId = lib.id, .title = "Amazing Grace"});
//
// Deleting an owner deletes what it owns (library -> presentations -> groups ->
// slides -> elements). Deleting a theme, arrangement or stage layout clears
// references to it. Deleting a presentation that a plan uses, or media that an
// element uses, is refused with Error::Code::Constraint.
template <typename T>
class Repository
{
public:
    explicit Repository(Database &db) : m_db(db) {}

    // Stores a new entity and returns it with its id filled in.
    Result<T> create(T entity);

    Result<T> get(const QString &id) const;

    // Every entity of this type, in display order.
    Result<std::vector<T>> list() const;

    // Children of one parent, in order (position, z-order or name). For
    // elements the parent may be a slide or a theme. Root entities (Library,
    // Theme, Plan, Output, StageLayout, Message, Timer) return Invalid.
    Result<std::vector<T>> listChildren(const QString &parentId) const;

    Status update(const T &entity);
    Status remove(const QString &id);

private:
    Database &m_db;
};

extern template class Repository<Library>;
extern template class Repository<Presentation>;
extern template class Repository<Group>;
extern template class Repository<Arrangement>;
extern template class Repository<Slide>;
extern template class Repository<Element>;
extern template class Repository<Theme>;
extern template class Repository<MediaAsset>;
extern template class Repository<Plan>;
extern template class Repository<PlanItem>;
extern template class Repository<Output>;
extern template class Repository<StageLayout>;
extern template class Repository<Message>;
extern template class Repository<Timer>;

} // namespace lumen::core::data
