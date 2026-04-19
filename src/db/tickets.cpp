#include "db/tickets.h"

#include "db/queries.h"
#include "db/statement.h"

#include <sqlite3.h>

#include <stdexcept>

namespace deltafeedback::db {

const char* to_string(TicketStatus s) {
    switch (s) {
        case TicketStatus::AwaitingAdmin:   return "awaiting_admin";
        case TicketStatus::AwaitingVisitor: return "awaiting_visitor";
        case TicketStatus::Closed:          return "closed";
    }
    return "awaiting_admin";  // unreachable
}

TicketStatus parse_status(const std::string& s) {
    if (s == "awaiting_admin")   return TicketStatus::AwaitingAdmin;
    if (s == "awaiting_visitor") return TicketStatus::AwaitingVisitor;
    if (s == "closed")           return TicketStatus::Closed;
    throw std::runtime_error("unknown ticket status: " + s);
}

void TicketsRepo::insert_new(const Ticket& t) {
    Statement s(db_, sql::kTicketInsert);
    s.bind_text(1, t.id);
    s.bind_blob(2, t.read_token_hash.data(), t.read_token_hash.size());
    s.bind_text(3, t.name);
    s.bind_text(4, t.locale);
    s.bind_int64(5, t.created_at);
    s.step();
}

std::optional<Ticket> TicketsRepo::get(const std::string& id) const {
    Statement s(db_, sql::kTicketGetById);
    s.bind_text(1, id);
    if (!s.step()) return std::nullopt;

    Ticket t;
    t.id              = s.col_text(0);
    t.read_token_hash = s.col_blob(1);
    t.name            = s.col_text(2);
    t.locale          = s.col_text(3);
    t.status          = parse_status(s.col_text(4));
    t.created_at      = s.col_int64(5);
    if (!s.col_is_null(6)) t.closed_at = s.col_int64(6);
    return t;
}

void TicketsRepo::set_status(const std::string& id, TicketStatus st) {
    Statement s(db_, sql::kTicketSetStatus);
    s.bind_text(1, id);
    s.bind_text(2, to_string(st));
    s.step();
}

void TicketsRepo::close(const std::string& id, std::int64_t closed_at) {
    Statement s(db_, sql::kTicketClose);
    s.bind_text(1, id);
    s.bind_int64(2, closed_at);
    s.step();
}

std::vector<TicketSummary> TicketsRepo::list_open() const {
    std::vector<TicketSummary> out;
    Statement s(db_, sql::kTicketListOpen);
    while (s.step()) {
        TicketSummary x;
        x.id         = s.col_text(0);
        x.status     = parse_status(s.col_text(1));
        x.created_at = s.col_int64(2);
        out.push_back(std::move(x));
    }
    return out;
}

int TicketsRepo::purge_inactive_older_than(std::int64_t cutoff_unix) {
    Statement s(db_, sql::kTicketPurgeInactive);
    s.bind_int64(1, cutoff_unix);
    s.step();
    return sqlite3_changes(db_);
}

}  // namespace deltafeedback::db
