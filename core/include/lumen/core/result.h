#pragma once

#include <QString>

#include <utility>
#include <variant>

namespace lumen::core {

// Why an operation failed. Errors are returned, never thrown, so a storage
// problem cannot take down the app during a service.
struct Error
{
    enum class Code {
        NotFound,     // no row with that id
        Constraint,   // foreign key, CHECK or UNIQUE rule refused the change
        Database,     // SQLite or driver failure
        SchemaTooNew, // database written by a newer Lumen than this one
        Migration,    // schema upgrade failed (database left unchanged)
        Invalid,      // the call itself makes no sense (e.g. wrong entity type)
        Unavailable,  // another process (e.g. the renderer) cannot be reached
    };

    Code code = Code::Database;
    QString message;
};

// Either a value or an Error.
template <typename T>
class [[nodiscard]] Result
{
public:
    Result(T value) : m_data(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : m_data(std::in_place_index<1>, std::move(error)) {}

    bool ok() const { return m_data.index() == 0; }
    explicit operator bool() const { return ok(); }

    T &value() & { return std::get<0>(m_data); }
    const T &value() const & { return std::get<0>(m_data); }
    T &&value() && { return std::get<0>(std::move(m_data)); }

    const Error &error() const { return std::get<1>(m_data); }

private:
    std::variant<T, Error> m_data;
};

struct Ok
{};

// Result of an operation that produces no value.
using Status = Result<Ok>;

inline Status success()
{
    return Ok{};
}

} // namespace lumen::core
