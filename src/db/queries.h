#pragma once

// All SQL lives here as named string literals.
//
// Rule for the project: NEVER assemble SQL with sprintf / string concat /
// fmt::format from user input. Add a new constant here and use it via
// db::Statement with bind_*. Table & column names are static; values are bound.

namespace deltafeedback::db::sql {

// --- Schema (single bootstrap statement, idempotent) ---
inline constexpr const char* kSchema = R"SQL(
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS state (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS tickets (
    id              TEXT PRIMARY KEY,           -- 6-char base32 e.g. A3F2K9
    read_token_hash BLOB NOT NULL,              -- sha256(read_token)
    name            TEXT NOT NULL DEFAULT '',
    locale          TEXT NOT NULL,              -- 'ru' | 'en'
    status          TEXT NOT NULL,              -- 'awaiting_admin' | 'awaiting_visitor' | 'closed'
    created_at      INTEGER NOT NULL,           -- unix seconds
    closed_at       INTEGER                     -- unix seconds, NULL while open
);

CREATE INDEX IF NOT EXISTS tickets_by_closed_at ON tickets(closed_at) WHERE closed_at IS NOT NULL;

CREATE TABLE IF NOT EXISTS messages (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ticket_id   TEXT    NOT NULL REFERENCES tickets(id) ON DELETE CASCADE,
    sender      TEXT    NOT NULL,               -- 'visitor' | 'admin'
    body        TEXT    NOT NULL,
    created_at  INTEGER NOT NULL,
    visitor_ip  TEXT,                           -- only set for visitor messages
    dc_msg_id   INTEGER                         -- bot's outgoing DC msg id (for quote-based reply routing)
);

CREATE INDEX IF NOT EXISTS messages_by_ticket    ON messages(ticket_id, id);
CREATE INDEX IF NOT EXISTS messages_by_dc_msg_id ON messages(dc_msg_id) WHERE dc_msg_id IS NOT NULL;

CREATE TABLE IF NOT EXISTS pow_replay (
    challenge_id BLOB PRIMARY KEY,              -- raw 16 random bytes from challenge.id
    expires_at   INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS pow_replay_by_expiry ON pow_replay(expires_at);
)SQL";

// --- state K/V ---
inline constexpr const char* kStateGet = "SELECT value FROM state WHERE key = ?1";
inline constexpr const char* kStateSet = "INSERT INTO state(key, value) VALUES(?1, ?2) "
                                         "ON CONFLICT(key) DO UPDATE SET value = excluded.value";
inline constexpr const char* kStateDel = "DELETE FROM state WHERE key = ?1";

// --- tickets ---
inline constexpr const char* kTicketInsert =
    "INSERT INTO tickets(id, read_token_hash, name, locale, status, created_at) "
    "VALUES(?1, ?2, ?3, ?4, 'awaiting_admin', ?5)";

inline constexpr const char* kTicketGetById =
    "SELECT id, read_token_hash, name, locale, status, created_at, closed_at "
    "FROM tickets WHERE id = ?1";

inline constexpr const char* kTicketSetStatus =
    "UPDATE tickets SET status = ?2 WHERE id = ?1 AND status != 'closed'";

inline constexpr const char* kTicketClose =
    "UPDATE tickets SET status = 'closed', closed_at = ?2 WHERE id = ?1 AND status != 'closed'";

inline constexpr const char* kTicketListOpen =
    "SELECT id, status, created_at FROM tickets WHERE status != 'closed' ORDER BY created_at DESC";

// Inactivity-based purge: drop any ticket whose latest event is older than
// the cutoff. "Latest event" = max(created_at, latest message.created_at,
// closed_at). Catches both abandoned-by-admin tickets and closed-but-stale
// ones in a single query.
inline constexpr const char* kTicketPurgeInactive =
    "DELETE FROM tickets WHERE MAX("
    "  IFNULL((SELECT MAX(created_at) FROM messages WHERE ticket_id = tickets.id), tickets.created_at),"
    "  IFNULL(closed_at, 0)"
    ") < ?1";

// --- messages ---
inline constexpr const char* kMessageInsert =
    "INSERT INTO messages(ticket_id, sender, body, created_at, visitor_ip, dc_msg_id) "
    "VALUES(?1, ?2, ?3, ?4, ?5, ?6)";

inline constexpr const char* kMessageSetDcMsgId =
    "UPDATE messages SET dc_msg_id = ?2 WHERE id = ?1";

inline constexpr const char* kMessagesByTicket =
    "SELECT id, sender, body, created_at FROM messages WHERE ticket_id = ?1 ORDER BY id";

// Quote-based reply routing: maps a bot-to-admin DC message id back to its
// originating ticket. Returns at most one row.
inline constexpr const char* kFindTicketByDcMsgId =
    "SELECT ticket_id FROM messages WHERE dc_msg_id = ?1 LIMIT 1";

// Latest dc_msg_id that the bot sent to admin for a given ticket. Used so
// the next outgoing notification can be sent as a DC reply (quote) of it,
// chaining the conversation in admin's client.
inline constexpr const char* kLatestDcMsgIdForTicket =
    "SELECT dc_msg_id FROM messages "
    "WHERE ticket_id = ?1 AND dc_msg_id IS NOT NULL "
    "ORDER BY id DESC LIMIT 1";

// --- pow_replay ---
// OR IGNORE makes the PK collision a no-op; caller checks sqlite3_changes()
// to distinguish first use (1) from replay (0). No exception needed.
inline constexpr const char* kReplayInsert =
    "INSERT OR IGNORE INTO pow_replay(challenge_id, expires_at) VALUES(?1, ?2)";

inline constexpr const char* kReplayPurge =
    "DELETE FROM pow_replay WHERE expires_at < ?1";

}  // namespace deltafeedback::db::sql
