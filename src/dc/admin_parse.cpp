#include "dc/admin_parse.h"

#include <cctype>

namespace deltafeedback::dc {

namespace {

bool is_id_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

std::string trim(std::string_view s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

}  // namespace

AdminCommand parse_admin_message(std::string_view text) {
    AdminCommand out;

    // Find the first [XXXXXX] (X = [A-Z0-9]). Hand-rolled scan beats <regex>
    // here on both compile time and runtime for a 6-char fixed pattern.
    size_t found_at = std::string_view::npos;
    for (size_t i = 0; i + 8 <= text.size(); ++i) {
        if (text[i] != '[') continue;
        if (text[i + 7] != ']') continue;
        bool ok = true;
        for (int k = 1; k <= 6; ++k) {
            if (!is_id_char(text[i + k])) { ok = false; break; }
        }
        if (ok) { found_at = i; break; }
    }

    if (found_at == std::string_view::npos) {
        out.kind = AdminCommand::NoTicketId;
        return out;
    }

    out.ticket_id.assign(text.data() + found_at + 1, 6);

    std::string body;
    body.reserve(text.size());
    body.append(text.data(), found_at);
    body.append(text.data() + found_at + 8, text.size() - found_at - 8);
    out.body = trim(body);

    if (out.body == "/close") {
        out.kind = AdminCommand::Close;
        out.body.clear();
    } else {
        out.kind = AdminCommand::Reply;
    }
    return out;
}

}  // namespace deltafeedback::dc
