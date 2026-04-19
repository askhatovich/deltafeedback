#include "db/database.h"

#include "db/queries.h"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace deltafeedback::db {

std::unique_ptr<Database> Database::open(const std::string& path) {
    std::unique_ptr<Database> d(new Database());
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path.c_str(), &d->db_, flags, nullptr) != SQLITE_OK) {
        std::string msg = "sqlite3_open_v2: ";
        if (d->db_) msg += sqlite3_errmsg(d->db_);
        throw std::runtime_error(msg);
    }
    d->apply_schema();
    return d;
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::apply_schema() {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql::kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = "schema: ";
        if (err) { msg += err; sqlite3_free(err); }
        throw std::runtime_error(msg);
    }
}

}  // namespace deltafeedback::db
