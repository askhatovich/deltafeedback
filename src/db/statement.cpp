#include "db/statement.h"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace deltafeedback::db {

namespace {
[[noreturn]] void die(sqlite3* db, const char* what) {
    std::string msg = what;
    if (db) { msg += ": "; msg += sqlite3_errmsg(db); }
    throw std::runtime_error(msg);
}
}  // namespace

Statement::Statement(sqlite3* db, const char* sql) : db_(db) {
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) die(db_, "prepare");
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& o) noexcept : db_(o.db_), stmt_(o.stmt_) {
    o.db_ = nullptr; o.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& o) noexcept {
    if (this != &o) {
        if (stmt_) sqlite3_finalize(stmt_);
        db_ = o.db_; stmt_ = o.stmt_;
        o.db_ = nullptr; o.stmt_ = nullptr;
    }
    return *this;
}

void Statement::bind_int(int i, int v) {
    if (sqlite3_bind_int(stmt_, i, v) != SQLITE_OK) die(db_, "bind_int");
}
void Statement::bind_int64(int i, std::int64_t v) {
    if (sqlite3_bind_int64(stmt_, i, v) != SQLITE_OK) die(db_, "bind_int64");
}
void Statement::bind_text(int i, std::string_view v) {
    if (sqlite3_bind_text(stmt_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        die(db_, "bind_text");
}
void Statement::bind_blob(int i, const void* data, std::size_t n) {
    if (sqlite3_bind_blob(stmt_, i, data, static_cast<int>(n), SQLITE_TRANSIENT) != SQLITE_OK)
        die(db_, "bind_blob");
}
void Statement::bind_null(int i) {
    if (sqlite3_bind_null(stmt_, i) != SQLITE_OK) die(db_, "bind_null");
}

bool Statement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW)  return true;
    if (rc == SQLITE_DONE) return false;
    die(db_, "step");
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

int Statement::col_int(int i) const { return sqlite3_column_int(stmt_, i); }
std::int64_t Statement::col_int64(int i) const { return sqlite3_column_int64(stmt_, i); }

std::string Statement::col_text(int i) const {
    auto* p = sqlite3_column_text(stmt_, i);
    int   n = sqlite3_column_bytes(stmt_, i);
    if (!p) return {};
    return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(n));
}

std::string Statement::col_blob(int i) const {
    auto* p = sqlite3_column_blob(stmt_, i);
    int   n = sqlite3_column_bytes(stmt_, i);
    if (!p) return {};
    return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(n));
}

bool Statement::col_is_null(int i) const {
    return sqlite3_column_type(stmt_, i) == SQLITE_NULL;
}

}  // namespace deltafeedback::db
