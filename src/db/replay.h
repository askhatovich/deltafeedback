#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct sqlite3;

namespace deltafeedback::db {

// Replay protection for POW challenges.
//
// `try_consume` returns true if the challenge_id was unused (and now records
// it). False means it was already used — the request must be rejected.
//
// PRIMARY KEY conflict on insert is the atomic check; no SELECT-then-INSERT
// race window.
class ReplayStore {
public:
    explicit ReplayStore(sqlite3* db) : db_(db) {}

    bool try_consume(std::string_view challenge_id_raw, std::int64_t expires_at);

    // Periodic cleanup. Call from a low-frequency timer.
    int purge_expired(std::int64_t now_unix);

private:
    sqlite3* db_;
};

}  // namespace deltafeedback::db
