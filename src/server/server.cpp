#include "server/server.h"

#include "db/messages.h"
#include "db/replay.h"
#include "db/tickets.h"
#include "dc/sender.h"
#include "feedback/service.h"
#include "feedback/validator.h"
#include "pow/challenge.h"
#include "pow/sha256.h"
#include "server/ratelimit.h"

#include "crow.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace deltafeedback::server {

namespace {

// Pulls the visitor IP. Order:
//   1. X-Forwarded-For (leftmost = original client; caddy/nginx convention)
//   2. X-Real-IP (alternative single-value header set by some proxies)
//   3. The connecting peer (req.remote_ip_address) — used in direct LAN dev
//
// We trust those headers because deployment policy is to sit behind a reverse
// proxy on 127.0.0.1. The dev path (browser → 0.0.0.0:8080 directly) just
// uses (3) since browsers don't set XFF.
//
// We trim whitespace, cap length (worst-case IPv6 ~45 chars), and drop any
// `:port` suffix or `[ipv6]` brackets — keeps the value safe to embed in the
// admin notification line and in logs.
std::string visitor_ip(const crow::request& req) {
    auto take_first = [](std::string s) {
        if (auto c = s.find(','); c != std::string::npos) s.resize(c);
        // trim
        size_t a = 0;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        size_t b = s.size();
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        s = s.substr(a, b - a);
        // strip [ipv6]:port → ipv6
        if (!s.empty() && s.front() == '[') {
            if (auto rb = s.find(']'); rb != std::string::npos) s = s.substr(1, rb - 1);
        } else if (std::count(s.begin(), s.end(), ':') == 1) {
            // ipv4:port → ipv4 (single colon = not ipv6)
            s.resize(s.find(':'));
        }
        if (s.size() > 64) s.resize(64);
        return s;
    };

    auto xff = req.get_header_value("X-Forwarded-For");
    if (!xff.empty()) return take_first(xff);

    auto xri = req.get_header_value("X-Real-IP");
    if (!xri.empty()) return take_first(xri);

    return req.remote_ip_address;
}

// Bearer-token extraction: returns the raw token string or "" if absent/malformed.
std::string bearer(const crow::request& req) {
    auto h = req.get_header_value("Authorization");
    constexpr const char* kPrefix = "Bearer ";
    if (h.rfind(kPrefix, 0) != 0) return "";
    return h.substr(std::strlen(kPrefix));
}

// Maps SubmitError → (status, code). The frontend uses `code` to look up
// localised text; we keep this table small and stable.
std::pair<int, const char*> map_submit_error(feedback::SubmitError e) {
    using E = feedback::SubmitError;
    switch (e) {
        case E::Ok:                 return {200, "ok"};
        case E::NoOwnerConfigured:  return {503, "no_owner"};
        case E::PowMissing:
        case E::PowInvalid:
        case E::PowExpired:
        case E::PowReplayed:
        case E::PowInsufficient:    return {400, "pow_failed"};
        case E::HoneypotTripped:    return {400, "honeypot"};
        case E::FilledTooFast:      return {400, "too_fast"};
        case E::BadField:           return {400, "bad_field"};
        case E::SendFailed:         return {502, "send_failed"};
    }
    return {500, "internal"};
}

std::pair<int, const char*> map_reply_error(feedback::Service::ReplyError e) {
    using E = feedback::Service::ReplyError;
    switch (e) {
        case E::Ok:                  return {200, "ok"};
        case E::NotFound:            return {404, "not_found"};
        case E::BadToken:            return {403, "bad_token"};
        case E::NotAwaitingVisitor:  return {409, "not_your_turn"};
        case E::Closed:              return {410, "ticket_closed"};
        case E::PowInvalid:
        case E::PowExpired:
        case E::PowReplayed:
        case E::PowInsufficient:
        case E::PowMissing:          return {400, "pow_failed"};
        case E::BadField:            return {400, "bad_field"};
        case E::SendFailed:          return {502, "send_failed"};
    }
    return {500, "internal"};
}

const char* mime_for(const std::string& path) {
    auto ext_at = path.rfind('.');
    if (ext_at == std::string::npos) return "application/octet-stream";
    auto ext = path.substr(ext_at);
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css")  return "text/css; charset=utf-8";
    if (ext == ".js")   return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg")  return "image/svg+xml";
    if (ext == ".png")  return "image/png";
    if (ext == ".ico")  return "image/x-icon";
    return "application/octet-stream";
}

crow::response file_response(const std::filesystem::path& root, const std::string& rel) {
    // Block traversal: lexically_normal + ensure result starts with root.
    auto full = std::filesystem::weakly_canonical(root / rel);
    auto rootc = std::filesystem::weakly_canonical(root);
    auto full_str = full.string();
    auto root_str = rootc.string();
    if (full_str.size() < root_str.size() || full_str.compare(0, root_str.size(), root_str) != 0) {
        return crow::response(404);
    }
    std::ifstream f(full, std::ios::binary);
    if (!f) return crow::response(404);
    std::ostringstream ss; ss << f.rdbuf();
    crow::response r(ss.str());
    r.add_header("Content-Type", mime_for(full_str));
    r.add_header("Cache-Control", "no-cache");
    return r;
}

std::string random_bytes(std::size_t n) {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::string s(n, '\0');
    for (std::size_t i = 0; i < n; i += 8) {
        std::uint64_t v = rng();
        for (std::size_t j = 0; j < 8 && i + j < n; ++j) s[i + j] = static_cast<char>((v >> (j * 8)) & 0xFF);
    }
    return s;
}

}  // namespace

struct Server::Impl {
    Settings                  settings;
    pow::ChallengeIssuer&     pow;
    feedback::Validator&      validator;
    dc::IMessageSender&       sender;
    db::TicketsRepo&          tickets;
    db::MessagesRepo&         messages;
    db::ReplayStore&          replay;

    feedback::Service         service;
    crow::SimpleApp           app;

    TokenBucket               rl_captcha {0.4, 20};   // 24/min sustained, burst 20
    TokenBucket               rl_submit  {0.1, 6};    // 6/min sustained, burst 6
    TokenBucket               rl_poll    {1.0, 60};   // 60/min sustained, burst 60

    Impl(Settings s,
         pow::ChallengeIssuer& p, feedback::Validator& v, dc::IMessageSender& sd,
         db::TicketsRepo& t, db::MessagesRepo& m, db::ReplayStore& r)
        : settings(std::move(s)), pow(p), validator(v), sender(sd),
          tickets(t), messages(m), replay(r),
          service(p, v, sd, t, m, r) {}
};

Server::Server(Settings s,
               pow::ChallengeIssuer& p, feedback::Validator& v, dc::IMessageSender& sd,
               db::TicketsRepo& t, db::MessagesRepo& m, db::ReplayStore& r)
    : impl_(std::make_unique<Impl>(std::move(s), p, v, sd, t, m, r)) {}

Server::~Server() = default;

int Server::run() {
    auto& app = impl_->app;
    auto& self = *impl_;

    // Disable Crow's own asio signal handling — main installs std::signal +
    // _Exit per project policy.
    app.signal_clear();

    std::filesystem::path web_root = self.settings.web_root;

    // --- Static / index ---
    CROW_ROUTE(app, "/")([web_root](const crow::request&) {
        return file_response(web_root, "index.html");
    });
    CROW_ROUTE(app, "/t/<string>")([web_root](const crow::request&, const std::string&) {
        return file_response(web_root, "index.html");
    });
    CROW_ROUTE(app, "/static/<string>")([web_root](const crow::request&, const std::string& p) {
        return file_response(web_root, p);
    });
    // Bare /favicon.ico — some clients (RSS readers, link-preview bots) hit
    // the root path directly instead of reading the <link rel="icon"> tag.
    CROW_ROUTE(app, "/favicon.ico")([web_root](const crow::request&) {
        return file_response(web_root, "favicon.ico");
    });
    CROW_ROUTE(app, "/static/locales/<string>")
    ([web_root](const crow::request&, const std::string& p) {
        return file_response(web_root, std::string("locales/") + p);
    });

    // --- Site config (read once by the frontend at load) ---
    CROW_ROUTE(app, "/api/site")([&self](const crow::request&) {
        crow::json::wvalue out;
        out["titles"]["ru"] = self.settings.title_ru;
        out["titles"]["en"] = self.settings.title_en;
        crow::response r{out};
        r.add_header("Content-Type", "application/json");
        return r;
    });

    // --- POW captcha ---
    CROW_ROUTE(app, "/api/captcha")
    ([&self](const crow::request& req) {
        if (!self.rl_captcha.allow(visitor_ip(req)))
            return crow::response(429, "{\"code\":\"rate_limited\"}");

        auto c = self.pow.issue(random_bytes(16));
        crow::json::wvalue out;
        out["token"]      = c.token;
        out["difficulty"] = c.difficulty_bits;
        out["expires_at"] = c.expires_unix;
        crow::response r{out};
        r.add_header("Content-Type", "application/json");
        return r;
    });

    // --- Submit new feedback ---
    CROW_ROUTE(app, "/api/feedback").methods(crow::HTTPMethod::POST)
    ([&self](const crow::request& req) {
        if (!self.rl_submit.allow(visitor_ip(req)))
            return crow::response(429, "{\"code\":\"rate_limited\"}");

        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"code\":\"bad_json\"}");

        feedback::Service::SubmitInput in;
        auto str = [&](const char* k) -> std::string {
            return body.has(k) && body[k].t() == crow::json::type::String
                       ? std::string(body[k].s()) : std::string();
        };
        in.pow_token    = str("pow_token");
        in.pow_nonce    = str("pow_nonce");
        in.name         = str("name");
        in.locale       = str("locale");
        in.honeypot     = str("honeypot");
        in.message      = str("message");
        in.fill_time_ms = body.has("fill_time_ms") ? static_cast<std::int64_t>(body["fill_time_ms"].i()) : 0;
        in.visitor_ip   = visitor_ip(req);

        auto r = self.service.submit_new(in);
        auto [status, code] = map_submit_error(r.error);

        crow::json::wvalue out;
        out["code"] = code;
        if (r.error == feedback::SubmitError::Ok) {
            out["ticket_id"]  = r.ticket_id;
            out["read_token"] = r.read_token;
        }
        crow::response resp{status, out};
        resp.add_header("Content-Type", "application/json");
        return resp;
    });

    // --- Get ticket state + messages ---
    CROW_ROUTE(app, "/api/tickets/<string>")
    ([&self](const crow::request& req, const std::string& id) {
        if (!self.rl_poll.allow(visitor_ip(req)))
            return crow::response(429, "{\"code\":\"rate_limited\"}");

        auto token = bearer(req);
        if (token.empty()) return crow::response(401, "{\"code\":\"missing_auth\"}");

        auto t = self.tickets.get(id);
        if (!t.has_value()) return crow::response(404, "{\"code\":\"not_found\"}");

        // Constant-time compare of read_token hash.
        auto h = pow::sha256(token);
        std::string h_str(reinterpret_cast<const char*>(h.data()), 32);
        unsigned char diff = h_str.size() != t->read_token_hash.size() ? 1 : 0;
        for (size_t i = 0; i < h_str.size() && i < t->read_token_hash.size(); ++i)
            diff |= static_cast<unsigned char>(h_str[i]) ^ static_cast<unsigned char>(t->read_token_hash[i]);
        if (diff != 0) return crow::response(403, "{\"code\":\"bad_token\"}");

        // ETag-like short-circuit: if the client tells us how many messages
        // and which status it already has and both still match, return 304
        // Not Modified with an empty body. Messages are append-only and
        // never edited, so a count match means "no new content".
        const std::string have_count_s  = req.url_params.get("have_count")  ? req.url_params.get("have_count")  : "";
        const std::string have_status_s = req.url_params.get("have_status") ? req.url_params.get("have_status") : "";
        auto msgs = self.messages.list(id);
        const std::string current_status = db::to_string(t->status);
        if (!have_count_s.empty() && !have_status_s.empty()) {
            try {
                if (static_cast<std::size_t>(std::stoul(have_count_s)) == msgs.size()
                    && have_status_s == current_status) {
                    return crow::response(304);
                }
            } catch (...) { /* malformed → fall through to full response */ }
        }

        crow::json::wvalue out;
        out["status"]    = current_status;
        std::vector<crow::json::wvalue> arr;
        arr.reserve(msgs.size());
        for (auto& m : msgs) {
            crow::json::wvalue row;
            row["sender"]     = (m.sender == db::Sender::Visitor) ? "visitor" : "admin";
            row["body"]       = m.body;
            row["created_at"] = m.created_at;
            arr.push_back(std::move(row));
        }
        out["messages"] = std::move(arr);

        crow::response r{out};
        r.add_header("Content-Type", "application/json");
        return r;
    });

    // --- Submit a follow-up reply on an open ticket ---
    CROW_ROUTE(app, "/api/tickets/<string>").methods(crow::HTTPMethod::POST)
    ([&self](const crow::request& req, const std::string& id) {
        if (!self.rl_submit.allow(visitor_ip(req)))
            return crow::response(429, "{\"code\":\"rate_limited\"}");

        auto token = bearer(req);
        if (token.empty()) return crow::response(401, "{\"code\":\"missing_auth\"}");

        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "{\"code\":\"bad_json\"}");

        feedback::Service::ReplyInput in;
        auto str = [&](const char* k) -> std::string {
            return body.has(k) && body[k].t() == crow::json::type::String
                       ? std::string(body[k].s()) : std::string();
        };
        in.ticket_id  = id;
        in.read_token = token;
        in.pow_token  = str("pow_token");
        in.pow_nonce  = str("pow_nonce");
        in.body       = str("body");
        in.visitor_ip = visitor_ip(req);

        auto err = self.service.submit_reply(in);
        auto [status, code] = map_reply_error(err);

        crow::json::wvalue out;
        out["code"] = code;
        crow::response resp{status, out};
        resp.add_header("Content-Type", "application/json");
        return resp;
    });

    app.bindaddr(self.settings.bind).port(self.settings.port).multithreaded().run();
    return 0;
}

void Server::stop() { impl_->app.stop(); }

}  // namespace deltafeedback::server
