#pragma once

#include <memory>
#include <mutex>
#include <string>

struct sqlite3;

namespace deltafeedback::db {

// Owns the sqlite3* handle. SQLite handles are thread-safe in the default
// build (SQLITE_THREADSAFE=1, "serialized" mode). We keep a single connection
// shared across the bot thread and Crow worker threads — simpler than per-thread
// pools and adequate for this load profile (a contact form, not a chat).
class Database {
public:
    static std::unique_ptr<Database> open(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* handle() { return db_; }

private:
    Database() = default;
    void apply_schema();

    sqlite3* db_ = nullptr;
};

}  // namespace deltafeedback::db
