#include "db/messages.h"

#include "db/queries.h"
#include "db/statement.h"

#include <sqlite3.h>

#include <stdexcept>

namespace deltafeedback::db {

namespace {

const char* sender_to_string(Sender s) {
    return s == Sender::Visitor ? "visitor" : "admin";
}

Sender parse_sender(const std::string& s) {
    if (s == "visitor") return Sender::Visitor;
    if (s == "admin")   return Sender::Admin;
    throw std::runtime_error("unknown sender: " + s);
}

}  // namespace

std::int64_t MessagesRepo::append(const std::string& ticket_id,
                                  Sender sender,
                                  const std::string& body,
                                  std::int64_t created_at,
                                  const std::string& visitor_ip,
                                  std::optional<std::uint32_t> dc_msg_id) {
    Statement s(db_, sql::kMessageInsert);
    s.bind_text(1, ticket_id);
    s.bind_text(2, sender_to_string(sender));
    s.bind_text(3, body);
    s.bind_int64(4, created_at);
    if (sender == Sender::Visitor && !visitor_ip.empty()) s.bind_text(5, visitor_ip);
    else                                                  s.bind_null(5);
    if (dc_msg_id.has_value()) s.bind_int64(6, *dc_msg_id);
    else                       s.bind_null(6);
    s.step();
    return sqlite3_last_insert_rowid(db_);
}

void MessagesRepo::set_dc_msg_id(std::int64_t row_id, std::uint32_t dc_msg_id) {
    Statement s(db_, sql::kMessageSetDcMsgId);
    s.bind_int64(1, row_id);
    s.bind_int64(2, dc_msg_id);
    s.step();
}

std::vector<Message> MessagesRepo::list(const std::string& ticket_id) const {
    std::vector<Message> out;
    Statement s(db_, sql::kMessagesByTicket);
    s.bind_text(1, ticket_id);
    while (s.step()) {
        Message m;
        m.id         = s.col_int64(0);
        m.ticket_id  = ticket_id;
        m.sender     = parse_sender(s.col_text(1));
        m.body       = s.col_text(2);
        m.created_at = s.col_int64(3);
        out.push_back(std::move(m));
    }
    return out;
}

std::optional<std::string> MessagesRepo::find_ticket_by_dc_msg_id(std::uint32_t dc_msg_id) const {
    Statement s(db_, sql::kFindTicketByDcMsgId);
    s.bind_int64(1, dc_msg_id);
    if (!s.step()) return std::nullopt;
    return s.col_text(0);
}

std::optional<std::uint32_t> MessagesRepo::latest_dc_msg_id_for_ticket(const std::string& ticket_id) const {
    Statement s(db_, sql::kLatestDcMsgIdForTicket);
    s.bind_text(1, ticket_id);
    if (!s.step()) return std::nullopt;
    return static_cast<std::uint32_t>(s.col_int64(0));
}

}  // namespace deltafeedback::db
