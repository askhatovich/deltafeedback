#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace deltafeedback::db {

// RAII wrapper around sqlite3_stmt. Designed so user-supplied data CANNOT end
// up as part of the SQL string — this is the primary anti-injection guard.
//
// Construction takes a const char* SQL **literal**. The constructor takes a
// `const char*` rather than `std::string` deliberately: it is harder to
// accidentally build a dynamic string and pass it in. Don't paper over that
// with std::string overloads.
class Statement {
public:
    Statement(sqlite3* db, const char* sql);
    ~Statement();

    Statement(Statement&& o) noexcept;
    Statement& operator=(Statement&& o) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // Bind by 1-based parameter index. SQLite handles typing.
    void bind_int(int i, int v);
    void bind_int64(int i, std::int64_t v);
    void bind_text(int i, std::string_view v);
    void bind_blob(int i, const void* data, std::size_t n);
    void bind_null(int i);

    // Step: returns true if a row is available, false at end. Throws on error.
    bool step();

    // Reset to be re-bound and re-stepped (cheaper than re-prepare).
    void reset();

    // Column accessors, 0-based.
    int          col_int(int i)   const;
    std::int64_t col_int64(int i) const;
    std::string  col_text(int i)  const;
    std::string  col_blob(int i)  const;
    bool         col_is_null(int i) const;

    sqlite3_stmt* raw() { return stmt_; }

private:
    sqlite3*      db_   = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace deltafeedback::db
