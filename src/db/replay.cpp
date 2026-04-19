#include "db/replay.h"

#include "db/queries.h"
#include "db/statement.h"

#include <sqlite3.h>

namespace deltafeedback::db {

bool ReplayStore::try_consume(std::string_view challenge_id_raw, std::int64_t expires_at) {
    Statement s(db_, sql::kReplayInsert);
    s.bind_blob(1, challenge_id_raw.data(), challenge_id_raw.size());
    s.bind_int64(2, expires_at);
    s.step();
    return sqlite3_changes(db_) == 1;
}

int ReplayStore::purge_expired(std::int64_t now_unix) {
    Statement s(db_, sql::kReplayPurge);
    s.bind_int64(1, now_unix);
    s.step();
    return sqlite3_changes(db_);
}

}  // namespace deltafeedback::db
