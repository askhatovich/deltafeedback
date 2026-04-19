#pragma once

#include <atomic>
#include <memory>
#include <string>

namespace deltafeedback::db { class Database; class TicketsRepo; class MessagesRepo; class StateRepo; }

namespace deltafeedback::dc {

struct OwnerHandle;
struct DcContextHandle;

// Owns the DC event loop. Construct once in main(); call configure_if_needed()
// before run_event_loop(). The loop blocks; stop() makes the next dc_get_next_event
// return (best-effort — actual shutdown is via std::_Exit in the signal handler).
class Bot {
public:
    Bot(DcContextHandle*    ctx,
        OwnerHandle&        owner,
        db::StateRepo&      state,
        db::TicketsRepo&    tickets,
        db::MessagesRepo&   messages);
    ~Bot();

    // Apply addr/mail_pw config and call dc_configure if dc_is_configured() is false.
    // Returns true on success.
    bool configure_if_needed(const std::string& addr, const std::string& mail_pw);

    // Apply a display name (visible to the admin in their DC client). Idempotent.
    void set_displayname(const std::string& name);

    // Print the securejoin invite QR/URL to stdout (call after configure when
    // owner is unset, so the operator can hand it to the future admin).
    void print_invite_qr();

    // Blocks. Returns 0 on graceful exit (stop()), 1 on fatal init error.
    int run_event_loop();

    // Best-effort: causes the event loop to break. _Exit(0) in the signal
    // handler is the actual shutdown path.
    void stop();

    // Static helper — used by `--reset-admin` before the bot starts.
    // Wipes owner_contact_id and removes any tickets/messages we held for
    // the previous owner.
    static void reset_owner_in_db(db::StateRepo& state, db::Database& db);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace deltafeedback::dc
