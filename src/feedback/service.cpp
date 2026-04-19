#include "feedback/service.h"

#include "db/messages.h"
#include "db/replay.h"
#include "db/tickets.h"
#include "dc/sender.h"
#include "feedback/validator.h"
#include "pow/challenge.h"
#include "pow/sha256.h"

#include <chrono>
#include <ctime>
#include <random>

namespace deltafeedback::feedback {

namespace {

std::int64_t now_unix() {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// 6-char base32 ticket id from a 30-bit random value. Crockford alphabet but
// without confusable chars (no I, L, O, U). Fits in 6 chars × 32 = 30 bits of
// entropy — fine, since IDs are short-lived and lookups are token-gated.
std::string make_ticket_id(std::mt19937_64& rng) {
    static const char* kAlpha = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // 32
    std::string id(6, '0');
    auto v = rng();
    for (int i = 0; i < 6; ++i) { id[i] = kAlpha[v & 0x1F]; v >>= 5; }
    return id;
}

std::string make_random(std::mt19937_64& rng, std::size_t bytes) {
    std::string s(bytes, '\0');
    for (std::size_t i = 0; i < bytes; i += 8) {
        std::uint64_t v = rng();
        for (std::size_t j = 0; j < 8 && i + j < bytes; ++j) {
            s[i + j] = static_cast<char>((v >> (j * 8)) & 0xFF);
        }
    }
    return s;
}

std::string format_iso8601_utc(std::int64_t t) {
    std::time_t tt = t;
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm);
    return buf;
}

std::string format_admin_notification(const std::string& ticket_id,
                                      const std::string& visitor_ip,
                                      std::int64_t when_unix,
                                      const std::string& locale,
                                      const std::string& name,
                                      const std::string& body,
                                      bool is_reply) {
    std::string out;
    out.reserve(64 + body.size());
    out.append("[").append(ticket_id).append("] ")
       .append(visitor_ip.empty() ? "?" : visitor_ip)
       .append(" \xc2\xb7 ")  // U+00B7 middle dot
       .append(format_iso8601_utc(when_unix))
       .append("\n");
    if (is_reply) {
        // Reply notifications are threaded under the initial via DC quote, so
        // a "reply from visitor" prefix is redundant — go straight to the body.
        out.push_back('\n');
    } else {
        if (!name.empty()) out.append(name);
        else               out.append("(anonymous)");
        out.append(" \xc2\xb7 ").append(locale).append("\n\n");
    }
    out.append(body);
    return out;
}

std::mt19937_64& thread_rng() {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    return rng;
}

}  // namespace

Service::Service(pow::ChallengeIssuer& p, Validator& v, dc::IMessageSender& s,
                 db::TicketsRepo& t, db::MessagesRepo& m, db::ReplayStore& r)
    : pow_(p), validator_(v), sender_(s), tickets_(t), messages_(m), replay_(r) {}

SubmitResult Service::submit_new(const SubmitInput& in) {
    SubmitResult res;

    // 1) POW signature/expiry check.
    std::string challenge_id;
    auto pv = pow_.verify(in.pow_token, in.pow_nonce, &challenge_id);
    switch (pv) {
        case pow::VerifyResult::Ok:               break;
        case pow::VerifyResult::Malformed:
        case pow::VerifyResult::BadSignature:     res.error = SubmitError::PowInvalid;     return res;
        case pow::VerifyResult::Expired:          res.error = SubmitError::PowExpired;     return res;
        case pow::VerifyResult::InsufficientWork: res.error = SubmitError::PowInsufficient; return res;
        case pow::VerifyResult::Replay:           res.error = SubmitError::PowReplayed;    return res;
    }

    // 2) Replay claim — atomic INSERT OR IGNORE.
    // We pass a generous expiry; the replay store TTL only controls cleanup.
    if (!replay_.try_consume(challenge_id, /*expires_at=*/now_unix() + 3600)) {
        res.error = SubmitError::PowReplayed;
        return res;
    }

    // 3) Field validation.
    SubmissionInput vi{
        in.name, in.message, in.locale, in.honeypot, in.fill_time_ms,
    };
    auto rep = validator_.validate(vi);
    if (!rep.ok()) {
        if (rep.honeypot != FieldError::Ok) res.error = SubmitError::HoneypotTripped;
        else if (rep.timing != FieldError::Ok) res.error = SubmitError::FilledTooFast;
        else res.error = SubmitError::BadField;
        return res;
    }

    // 4) Owner check (no owner → don't even insert).
    if (!sender_.owner_contact_id().has_value()) {
        res.error = SubmitError::NoOwnerConfigured;
        return res;
    }

    // 5) Insert ticket + first message (with NULL dc_msg_id; we update after send).
    auto& rng = thread_rng();
    std::string id        = make_ticket_id(rng);
    std::string token_hex = pow::to_hex(make_random(rng, 32));  // 64 hex chars

    db::Ticket t;
    t.id              = id;
    t.read_token_hash = std::string(reinterpret_cast<const char*>(pow::sha256(token_hex).data()), 32);
    t.name            = in.name;
    t.locale          = in.locale;
    t.status          = db::TicketStatus::AwaitingAdmin;
    t.created_at      = now_unix();
    tickets_.insert_new(t);

    auto row_id = messages_.append(id, db::Sender::Visitor, in.message, t.created_at, in.visitor_ip);

    // 6) Notify admin. If DC send fails, the ticket is still in the DB;
    // admin can recover via /list once the bot reconnects.
    auto note = format_admin_notification(id, in.visitor_ip, t.created_at,
                                          in.locale, in.name, in.message,
                                          /*is_reply=*/false);
    auto dc_msg_id = sender_.send_to_owner(note);
    if (dc_msg_id) {
        messages_.set_dc_msg_id(row_id, *dc_msg_id);
    } else {
        res.error = SubmitError::SendFailed;
    }

    res.ticket_id  = id;
    res.read_token = std::move(token_hex);  // returned ONCE; client stores in localStorage
    return res;
}

Service::ReplyError Service::submit_reply(const ReplyInput& in) {
    auto t = tickets_.get(in.ticket_id);
    if (!t.has_value()) return ReplyError::NotFound;

    // Constant-time compare of read_token hash.
    auto h = pow::sha256(in.read_token);
    std::string h_str(reinterpret_cast<const char*>(h.data()), 32);
    if (h_str.size() != t->read_token_hash.size()) return ReplyError::BadToken;
    unsigned char diff = 0;
    for (size_t i = 0; i < h_str.size(); ++i)
        diff |= static_cast<unsigned char>(h_str[i]) ^ static_cast<unsigned char>(t->read_token_hash[i]);
    if (diff != 0) return ReplyError::BadToken;

    if (t->status == db::TicketStatus::Closed)          return ReplyError::Closed;
    if (t->status != db::TicketStatus::AwaitingVisitor) return ReplyError::NotAwaitingVisitor;

    // POW per reply (same defence as the initial submit).
    std::string challenge_id;
    auto pv = pow_.verify(in.pow_token, in.pow_nonce, &challenge_id);
    switch (pv) {
        case pow::VerifyResult::Ok: break;
        case pow::VerifyResult::Malformed:
        case pow::VerifyResult::BadSignature:     return ReplyError::PowInvalid;
        case pow::VerifyResult::Expired:          return ReplyError::PowExpired;
        case pow::VerifyResult::InsufficientWork: return ReplyError::PowInsufficient;
        case pow::VerifyResult::Replay:           return ReplyError::PowReplayed;
    }
    if (!replay_.try_consume(challenge_id, now_unix() + 3600)) return ReplyError::PowReplayed;

    if (validator_.validate_reply(in.body) != FieldError::Ok) return ReplyError::BadField;

    auto when = now_unix();
    // Find the bot's most recent outgoing notification for this ticket, so
    // the new one is sent as a DC reply to it (visible threading in admin's
    // client). Captured BEFORE we insert the new row — that row's dc_msg_id
    // is still NULL anyway, but reading first keeps the intent explicit.
    auto quote_id = messages_.latest_dc_msg_id_for_ticket(in.ticket_id).value_or(0);

    auto row_id = messages_.append(in.ticket_id, db::Sender::Visitor, in.body, when, in.visitor_ip);
    tickets_.set_status(in.ticket_id, db::TicketStatus::AwaitingAdmin);

    auto note = format_admin_notification(in.ticket_id, in.visitor_ip, when,
                                          t->locale, t->name, in.body,
                                          /*is_reply=*/true);
    auto dc_msg_id = sender_.send_to_owner(note, quote_id);
    if (!dc_msg_id) return ReplyError::SendFailed;
    messages_.set_dc_msg_id(row_id, *dc_msg_id);
    return ReplyError::Ok;
}

}  // namespace deltafeedback::feedback
