#include "feedback/validator.h"

namespace deltafeedback::feedback {

std::size_t utf8_codepoint_count(std::string_view s) {
    std::size_t n = 0;
    for (unsigned char c : s) {
        // count only "leading" bytes: 0xxxxxxx or 11xxxxxx; skip 10xxxxxx continuation bytes
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

bool has_disallowed_control_chars(std::string_view s) {
    for (unsigned char c : s) {
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return true;
        if (c == 0x7F) return true;  // DEL
    }
    return false;
}

namespace {

FieldError check_text(std::string_view s, std::size_t max_chars, bool allow_empty) {
    if (s.empty()) return allow_empty ? FieldError::Ok : FieldError::Empty;
    if (has_disallowed_control_chars(s)) return FieldError::BadChars;
    if (utf8_codepoint_count(s) > max_chars) return FieldError::TooLong;
    return FieldError::Ok;
}

}  // namespace

ValidationReport Validator::validate(const SubmissionInput& in) const {
    ValidationReport r;

    if (!in.honeypot.empty()) r.honeypot = FieldError::HoneypotTripped;
    if (in.fill_time_ms < limits_.min_fill_time_ms) r.timing = FieldError::FilledTooFast;

    r.name     = check_text(in.name,     limits_.name_max_chars,    /*allow_empty=*/true);
    r.message  = check_text(in.message,  limits_.message_max_chars, /*allow_empty=*/false);

    if (in.locale != "ru" && in.locale != "en") r.locale = FieldError::BadLocale;

    return r;
}

FieldError Validator::validate_reply(std::string_view text) const {
    return check_text(text, limits_.message_max_chars, /*allow_empty=*/false);
}

}  // namespace deltafeedback::feedback
