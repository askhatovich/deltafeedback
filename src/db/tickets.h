#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace deltafeedback::db {

enum class TicketStatus { AwaitingAdmin, AwaitingVisitor, Closed };

struct Ticket {
    std::string  id;                 // 6-char base32
    std::string  read_token_hash;    // 32 raw bytes
    std::string  name;
    std::string  locale;             // "ru" | "en"
    TicketStatus status;
    std::int64_t created_at;
    std::optional<std::int64_t> closed_at;
};

struct TicketSummary {
    std::string  id;
    TicketStatus status;
    std::int64_t created_at;
};

// Repository for `tickets` table.
//
// Rules:
//   - All ticket IDs generated here, never accepted from clients (we look up
//     tickets by URL but verify the read_token_hash before serving).
//   - read_token_hash is sha256 of the raw token, never the token itself.
class TicketsRepo {
public:
    explicit TicketsRepo(sqlite3* db) : db_(db) {}

    // Inserts a new ticket in AwaitingAdmin state. Caller supplies the ID
    // (generated upstream so the same value is returned to the client + logged).
    void insert_new(const Ticket& t);

    std::optional<Ticket> get(const std::string& id) const;

    void set_status(const std::string& id, TicketStatus s);
    void close(const std::string& id, std::int64_t closed_at);

    std::vector<TicketSummary> list_open() const;

    // Bulk delete of tickets whose latest event (created_at, latest message,
    // closed_at) is older than `cutoff_unix`. Cascades to messages via FK
    // ON DELETE CASCADE. Returns rows affected.
    int purge_inactive_older_than(std::int64_t cutoff_unix);

private:
    sqlite3* db_;
};

const char* to_string(TicketStatus s);
TicketStatus parse_status(const std::string& s);  // throws on garbage

}  // namespace deltafeedback::db
