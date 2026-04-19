#pragma once

#include <optional>
#include <string>

struct sqlite3;

namespace deltafeedback::db {

// Tiny K/V over the `state` table. Stable keys used across the app:
//   "owner_contact_id"  — current admin's DC contact id (decimal)
//   "hmac_secret"       — POW HMAC secret (hex)
class StateRepo {
public:
    explicit StateRepo(sqlite3* db) : db_(db) {}

    std::optional<std::string> get(const std::string& key) const;
    void set(const std::string& key, const std::string& value);
    void erase(const std::string& key);

private:
    sqlite3* db_;
};

}  // namespace deltafeedback::db
