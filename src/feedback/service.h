#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace deltafeedback::pow      { class ChallengeIssuer; }
namespace deltafeedback::feedback { class Validator; }
namespace deltafeedback::dc       { class IMessageSender; }
namespace deltafeedback::db       { class TicketsRepo; class MessagesRepo; class ReplayStore; }

namespace deltafeedback::feedback {

// Outcome of a submission attempt — translated by the HTTP layer into status
// codes and i18n keys.
enum class SubmitError {
    Ok,
    NoOwnerConfigured,    // 503 — admin reset, nobody added the bot yet
    PowMissing,
    PowInvalid,
    PowReplayed,
    PowExpired,
    PowInsufficient,
    HoneypotTripped,
    FilledTooFast,
    BadField,
    SendFailed,
};

struct SubmitResult {
    SubmitError error = SubmitError::Ok;
    std::string ticket_id;     // populated on Ok
    std::string read_token;    // populated on Ok — return ONCE, never again
};

// Coordinates: pow verify → replay claim → validate → insert ticket → format
// admin notification → send via DC. Single entry point keeps HTTP handlers tiny.
class Service {
public:
    Service(pow::ChallengeIssuer& pow_issuer,
            Validator&            validator,
            dc::IMessageSender&   sender,
            db::TicketsRepo&      tickets,
            db::MessagesRepo&     messages,
            db::ReplayStore&      replay);

    struct SubmitInput {
        std::string pow_token;
        std::string pow_nonce;
        std::string name;       // optional
        std::string locale;     // "ru" | "en"
        std::string honeypot;
        std::string message;
        std::int64_t fill_time_ms = 0;
        std::string visitor_ip;
    };

    SubmitResult submit_new(const SubmitInput& in);

    enum class ReplyError {
        Ok, NotFound, BadToken, NotAwaitingVisitor, Closed,
        PowMissing, PowInvalid, PowReplayed, PowExpired, PowInsufficient,
        BadField, SendFailed,
    };

    struct ReplyInput {
        std::string ticket_id;
        std::string read_token;
        std::string pow_token;
        std::string pow_nonce;
        std::string body;
        std::string visitor_ip;
    };

    ReplyError submit_reply(const ReplyInput& in);

private:
    pow::ChallengeIssuer& pow_;
    Validator&            validator_;
    dc::IMessageSender&   sender_;
    db::TicketsRepo&      tickets_;
    db::MessagesRepo&     messages_;
    db::ReplayStore&      replay_;
};

}  // namespace deltafeedback::feedback
