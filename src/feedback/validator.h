#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace deltafeedback::feedback {

// Limits, all configurable from config.ini at construction.
struct Limits {
    std::size_t message_max_chars  = 500;
    std::size_t name_max_chars     = 40;
    std::int64_t min_fill_time_ms  = 1500;
};

enum class FieldError {
    Ok,
    Empty,
    TooLong,
    BadChars,        // contains control chars (other than \n / \t)
    BadLocale,       // locale not in {ru, en}
    HoneypotTripped, // honeypot field non-empty
    FilledTooFast,   // form was submitted faster than min_fill_time_ms
};

struct ValidationReport {
    FieldError name     = FieldError::Ok;  // optional, only checked when non-empty
    FieldError message  = FieldError::Ok;
    FieldError locale   = FieldError::Ok;
    FieldError honeypot = FieldError::Ok;
    FieldError timing   = FieldError::Ok;

    bool ok() const {
        return name == FieldError::Ok && message == FieldError::Ok &&
               locale == FieldError::Ok && honeypot == FieldError::Ok &&
               timing == FieldError::Ok;
    }
};

struct SubmissionInput {
    std::string_view name;        // optional
    std::string_view message;
    std::string_view locale;      // "ru" or "en"
    std::string_view honeypot;    // must be empty
    std::int64_t fill_time_ms = 0;
};

class Validator {
public:
    explicit Validator(Limits limits) : limits_(limits) {}

    ValidationReport validate(const SubmissionInput& in) const;

    // Validates a follow-up reply on an open ticket — only the message body matters.
    FieldError validate_reply(std::string_view text) const;

    const Limits& limits() const { return limits_; }

private:
    Limits limits_;
};

// Counts UTF-8 *codepoints*, not bytes. Used for length limits — a byte-length
// limit would let a Russian message be effectively 250 chars while an English
// one is 500.
std::size_t utf8_codepoint_count(std::string_view s);

// True if s contains any C0 control char other than \t (0x09) and \n (0x0A).
// Used to reject NUL/escape sequences in form fields.
bool has_disallowed_control_chars(std::string_view s);

}  // namespace deltafeedback::feedback
