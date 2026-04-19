#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace deltafeedback::db {

enum class Sender { Visitor, Admin };

struct Message {
    std::int64_t id;
    std::string  ticket_id;
    Sender       sender;
    std::string  body;
    std::int64_t created_at;
};

class MessagesRepo {
public:
    explicit MessagesRepo(sqlite3* db) : db_(db) {}

    // Returns the new row's id (sqlite3_last_insert_rowid). `dc_msg_id` is
    // only meaningful for visitor messages — it is the bot's outgoing DC
    // notification id, used later to resolve admin's quoted reply back to
    // a ticket. Pass std::nullopt for admin messages or when the bot send
    // failed (admin can recover via /list).
    std::int64_t append(const std::string& ticket_id,
                        Sender sender,
                        const std::string& body,
                        std::int64_t created_at,
                        const std::string& visitor_ip,
                        std::optional<std::uint32_t> dc_msg_id = std::nullopt);

    // Late-binding for the case where we want the row inserted before the
    // DC send completes (so the schema row exists even if send fails).
    void set_dc_msg_id(std::int64_t row_id, std::uint32_t dc_msg_id);

    std::vector<Message> list(const std::string& ticket_id) const;

    // Reverse lookup for quote-based admin reply routing.
    std::optional<std::string> find_ticket_by_dc_msg_id(std::uint32_t dc_msg_id) const;

    // Most recent bot-to-admin DC msg id for this ticket (used so the next
    // outgoing notification can quote it, threading the conversation).
    std::optional<std::uint32_t> latest_dc_msg_id_for_ticket(const std::string& ticket_id) const;

private:
    sqlite3* db_;
};

}  // namespace deltafeedback::db
