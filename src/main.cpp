// deltafeedback — entry point.

#include "app/config.h"
#include "db/database.h"
#include "db/messages.h"
#include "db/replay.h"
#include "db/state.h"
#include "db/tickets.h"
#include "dc/bot.h"
#include "dc/sender.h"
#include "feedback/validator.h"
#include "pow/challenge.h"
#include "pow/sha256.h"
#include "server/server.h"

#include <deltachat.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>

using namespace deltafeedback;

namespace {

void usage(const char* prog) {
    std::printf(
        "Usage:\n"
        "  %s --register <chatmail-domain> [config]\n"
        "      Register on a chatmail server. Credentials are written to the\n"
        "      account file (config's account_path key) — falls back to the\n"
        "      main config file when account_path is unset.\n"
        "  %s --run [config]\n"
        "      Start the web server and DC bot. Default config: ./config.ini\n"
        "  %s --invite [config]\n"
        "      Print the Delta Chat invite URL/QR on stdout. Useful after\n"
        "      `--reset-admin` or under systemd where stdout goes to journald.\n"
        "  %s --reset-admin [config]\n"
        "      Forget current owner; on next --run a fresh invite is printed.\n"
        "      Drops all tickets and pending POW state addressed to the former owner.\n",
        prog, prog, prog, prog);
}

const char* arg_or(int argc, char** argv, int i, const char* fallback) {
    return i < argc ? argv[i] : fallback;
}

[[noreturn]] void signal_handler(int) { std::_Exit(0); }

void install_signal_handlers() {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);
}

std::string random_hex(std::size_t bytes) {
    std::mt19937_64 rng{std::random_device{}()};
    std::string out(bytes * 2, '0');
    static const char* h = "0123456789abcdef";
    for (std::size_t i = 0; i < bytes; ++i) {
        std::uint8_t b = static_cast<std::uint8_t>(rng() & 0xFF);
        out[2 * i]     = h[b >> 4];
        out[2 * i + 1] = h[b & 0xF];
    }
    return out;
}

// Path where mutable runtime values (addr, mail_pw, hmac_secret, …) are
// written. If config.ini sets account_path → that file. Otherwise → the main
// config itself (single-file dev layout).
std::string writable_path(const app::Config& cfg, const std::string& main_path) {
    auto a = cfg.get("account_path");
    return a.empty() ? main_path : a;
}

// Loads the main config, then overlays values from account_path (if set).
// Lets /etc/deltafeedback/config.ini be root-owned read-only while the
// service writes runtime credentials into /var/lib/deltafeedback/account.ini.
app::Config load_with_account(const std::string& main_path) {
    auto cfg = app::Config::load(main_path);
    auto acc = cfg.get("account_path");
    if (!acc.empty()) {
        auto overlay = app::Config::load(acc);
        for (const auto& [k, v] : overlay.raw()) cfg.set(k, v);
    }
    return cfg;
}

// Saves only the supplied keys to the writable file. Preserves anything
// else that's already there.
bool save_account(const std::string& path, std::initializer_list<std::pair<std::string, std::string>> kv) {
    auto existing = app::Config::load(path);
    for (auto& [k, v] : kv) existing.set(k, v);
    return existing.save(path);
}

std::string ensure_hmac_secret(app::Config& cfg, const std::string& writable) {
    auto s = cfg.get("hmac_secret");
    if (!s.empty()) return s;
    s = random_hex(32);
    cfg.set("hmac_secret", s);
    if (!save_account(writable, {{"hmac_secret", s}})) {
        std::fprintf(stderr, "[setup] WARNING: could not write hmac_secret to %s — "
                             "POW tokens will be invalidated on every restart.\n",
                     writable.c_str());
    } else {
        std::printf("[setup] generated hmac_secret and saved to %s\n", writable.c_str());
    }
    return s;
}

std::string hex_decode(const std::string& hex) {
    std::string out;
    out.reserve(hex.size() / 2);
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
    };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int hi = v(hex[i]), lo = v(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

bool wait_configure(dc_event_emitter_t* em) {
    while (true) {
        dc_event_t* ev = dc_get_next_event(em);
        if (!ev) return false;
        int id = dc_event_get_id(ev);
        if (id == DC_EVENT_CONFIGURE_PROGRESS) {
            int p = dc_event_get_data1_int(ev);
            std::printf("[register] %d/1000\n", p);
            if (p == 1000) { dc_event_unref(ev); return true; }
            if (p == 0)    { dc_event_unref(ev); return false; }
        }
        dc_event_unref(ev);
    }
}

int cmd_register(const std::string& domain, const std::string& cfg_path) {
    app::Config cfg = load_with_account(cfg_path);
    std::string writable = writable_path(cfg, cfg_path);
    std::string db_path  = cfg.get("db", "./deltafeedback.db");

    // Use a SEPARATE DC database next to ours (state lives in our SQLite).
    std::string dc_db = db_path + ".dc";

    dc_context_t* ctx = dc_context_new(nullptr, dc_db.c_str(), nullptr);
    if (!ctx) { std::fprintf(stderr, "dc_context_new failed\n"); return 1; }
    dc_event_emitter_t* em = dc_get_event_emitter(ctx);

    std::string qr = "DCACCOUNT:https://" + domain + "/new";
    dc_lot_t* lot = dc_check_qr(ctx, qr.c_str());
    if (!lot || dc_lot_get_state(lot) != DC_QR_ACCOUNT) {
        std::fprintf(stderr, "Not a valid chatmail domain: %s\n", domain.c_str());
        if (lot) dc_lot_unref(lot);
        dc_event_emitter_unref(em);
        dc_context_unref(ctx);
        return 1;
    }
    dc_lot_unref(lot);

    if (!dc_set_config_from_qr(ctx, qr.c_str())) {
        std::fprintf(stderr, "dc_set_config_from_qr failed\n");
        dc_event_emitter_unref(em); dc_context_unref(ctx); return 1;
    }
    dc_set_config(ctx, "bot", "1");
    dc_set_config(ctx, "e2ee_enabled", "1");
    dc_configure(ctx);
    bool ok = wait_configure(em);

    char* addr = dc_get_config(ctx, "configured_addr");
    char* pw   = dc_get_config(ctx, "mail_pw");
    std::string addr_s = addr ? addr : "";
    std::string pw_s   = pw   ? pw   : "";
    if (addr) dc_str_unref(addr);
    if (pw)   dc_str_unref(pw);

    dc_event_emitter_unref(em);
    dc_context_unref(ctx);

    if (!ok || addr_s.empty() || pw_s.empty()) {
        std::fprintf(stderr, "Registration failed.\n");
        return 1;
    }

    if (!save_account(writable, {
            {"addr",    addr_s},
            {"mail_pw", pw_s},
            {"db",      db_path},
        })) {
        std::fprintf(stderr, "Could not write account file %s — check permissions.\n",
                     writable.c_str());
        return 1;
    }

    std::printf("Registered: %s\nAccount file: %s\nDC db:        %s\n",
                addr_s.c_str(), writable.c_str(), dc_db.c_str());
    std::printf("Run:   deltafeedback --run %s\n", cfg_path.c_str());
    return 0;
}

int cmd_invite(const std::string& cfg_path) {
    auto cfg = load_with_account(cfg_path);
    auto db_path = cfg.get("db", "./deltafeedback.db");
    auto dc_db   = db_path + ".dc";

    dc_context_t* ctx = dc_context_new(nullptr, dc_db.c_str(), nullptr);
    if (!ctx) { std::fprintf(stderr, "dc_context_new failed (%s)\n", dc_db.c_str()); return 1; }

    if (!dc_is_configured(ctx)) {
        std::fprintf(stderr, "DC not configured — run --register first.\n");
        dc_context_unref(ctx);
        return 1;
    }

    char* qr = dc_get_securejoin_qr(ctx, 0);
    if (!qr) {
        std::fprintf(stderr, "Could not generate invite QR.\n");
        dc_context_unref(ctx);
        return 1;
    }
    std::printf("%s\n", qr);
    dc_str_unref(qr);
    dc_context_unref(ctx);
    return 0;
}

int cmd_reset_admin(const std::string& cfg_path) {
    auto cfg = load_with_account(cfg_path);
    auto db_path = cfg.get("db", "./deltafeedback.db");
    auto db = db::Database::open(db_path);
    db::StateRepo state(db->handle());
    dc::Bot::reset_owner_in_db(state, *db);
    std::printf("Admin reset. Restart with --run; the next contact will become admin.\n");
    return 0;
}

int cmd_run(const std::string& cfg_path) {
    auto cfg = load_with_account(cfg_path);
    if (!cfg.has("addr") || !cfg.has("mail_pw")) {
        std::fprintf(stderr, "Config missing addr/mail_pw — run --register first.\n");
        return 1;
    }
    auto db_path  = cfg.get("db", "./deltafeedback.db");
    auto dc_db    = db_path + ".dc";
    auto bind     = cfg.get("bind", "0.0.0.0");
    auto port     = static_cast<std::uint16_t>(cfg.get_int("port", 8080));
    auto web_root = cfg.get("web_root", "./web");
    auto writable = writable_path(cfg, cfg_path);

    auto secret_hex = ensure_hmac_secret(cfg, writable);
    auto secret     = hex_decode(secret_hex);

    auto db = db::Database::open(db_path);
    db::StateRepo    state(db->handle());
    db::TicketsRepo  tickets(db->handle());
    db::MessagesRepo messages(db->handle());
    db::ReplayStore  replay(db->handle());

    pow::ChallengeIssuer pow_issuer(
        secret,
        static_cast<unsigned>(cfg.get_int("pow_difficulty_bits", 18)),
        std::chrono::seconds(cfg.get_int("pow_challenge_ttl_seconds", 300)));

    feedback::Limits limits;
    limits.message_max_chars  = static_cast<std::size_t>(cfg.get_int("message_max_chars", 500));
    limits.name_max_chars     = static_cast<std::size_t>(cfg.get_int("name_max_chars", 40));
    limits.min_fill_time_ms   = cfg.get_int("min_fill_time_ms", 1500);
    feedback::Validator validator(limits);

    // DC context.
    dc_context_t* ctx = dc_context_new(nullptr, dc_db.c_str(), nullptr);
    if (!ctx) { std::fprintf(stderr, "dc_context_new failed\n"); return 1; }
    auto* ctx_handle = reinterpret_cast<dc::DcContextHandle*>(ctx);

    dc::OwnerHandle owner;
    dc::Bot bot(ctx_handle, owner, state, tickets, messages);
    if (!bot.configure_if_needed(cfg.get("addr"), cfg.get("mail_pw"))) {
        std::fprintf(stderr, "DC configure failed\n");
        dc_context_unref(ctx);
        return 1;
    }
    bot.set_displayname(cfg.get("displayname"));
    if (owner.contact_id.load() == 0) bot.print_invite_qr();

    dc::DcSender sender(ctx_handle, owner);

    // Background purges (closed tickets after 7 days, expired POW replay).
    int retention_days = cfg.get_int("closed_ticket_retention_days", 7);
    std::thread purge_thread([&] {
        while (true) {
            std::this_thread::sleep_for(std::chrono::hours(1));
            auto now = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            tickets.purge_inactive_older_than(now - retention_days * 86400);
            replay.purge_expired(now);
        }
    });
    purge_thread.detach();

    // Bot in its own thread.
    std::thread bot_thread([&] { bot.run_event_loop(); });
    bot_thread.detach();  // _Exit(0) shuts everything down

    // Server in main thread (blocks).
    server::Settings s;
    s.bind = bind; s.port = port; s.web_root = web_root;
    server::Server srv(s, pow_issuer, validator, sender, tickets, messages, replay);
    std::printf("Server listening on %s:%u (web_root=%s)\n", bind.c_str(), port, web_root.c_str());
    return srv.run();
}

}  // namespace

int main(int argc, char** argv) {
    install_signal_handlers();

    if (argc < 2) { usage(argv[0]); return 2; }
    std::string cmd = argv[1];

    if (cmd == "--register") {
        if (argc < 3) { usage(argv[0]); return 2; }
        return cmd_register(argv[2], arg_or(argc, argv, 3, "./config.ini"));
    }
    if (cmd == "--run")          return cmd_run(arg_or(argc, argv, 2, "./config.ini"));
    if (cmd == "--invite")       return cmd_invite(arg_or(argc, argv, 2, "./config.ini"));
    if (cmd == "--reset-admin")  return cmd_reset_admin(arg_or(argc, argv, 2, "./config.ini"));

    usage(argv[0]);
    return 2;
}
