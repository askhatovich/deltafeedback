#include "db/state.h"

#include "db/queries.h"
#include "db/statement.h"

namespace deltafeedback::db {

std::optional<std::string> StateRepo::get(const std::string& key) const {
    Statement s(db_, sql::kStateGet);
    s.bind_text(1, key);
    if (!s.step()) return std::nullopt;
    return s.col_text(0);
}

void StateRepo::set(const std::string& key, const std::string& value) {
    Statement s(db_, sql::kStateSet);
    s.bind_text(1, key);
    s.bind_text(2, value);
    s.step();
}

void StateRepo::erase(const std::string& key) {
    Statement s(db_, sql::kStateDel);
    s.bind_text(1, key);
    s.step();
}

}  // namespace deltafeedback::db
