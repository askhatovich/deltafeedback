#include "dc/admin_route.h"

#include "dc/admin_parse.h"

#include <cctype>

namespace deltafeedback::dc {

namespace {

std::string trim(std::string_view s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

AdminRoute make_kind(std::string ticket_id, std::string body) {
    AdminRoute r;
    r.ticket_id = std::move(ticket_id);
    if (body == "/close") { r.kind = AdminRoute::Close; r.body.clear(); }
    else                  { r.kind = AdminRoute::Reply; r.body = std::move(body); }
    return r;
}

}  // namespace

AdminRoute route_admin_message(
    std::string_view text,
    std::uint32_t    quoted_msg_id,
    std::string_view quoted_text,
    const std::function<std::optional<std::string>(std::uint32_t)>& lookup) {

    // (1) DC reply / quote, where the quoted message resolves in our DB.
    if (quoted_msg_id != 0 && lookup) {
        if (auto t = lookup(quoted_msg_id)) {
            return make_kind(*t, trim(text));
        }
    }

    // (2) [ID] typed in admin's body.
    if (auto cmd = parse_admin_message(text); cmd.kind != AdminCommand::NoTicketId) {
        AdminRoute r;
        r.ticket_id = std::move(cmd.ticket_id);
        r.kind      = (cmd.kind == AdminCommand::Close) ? AdminRoute::Close : AdminRoute::Reply;
        r.body      = std::move(cmd.body);
        return r;
    }

    // (3) [ID] in the quoted text (cross-device fallback).
    if (auto qcmd = parse_admin_message(quoted_text); qcmd.kind != AdminCommand::NoTicketId) {
        return make_kind(std::move(qcmd.ticket_id), trim(text));
    }

    AdminRoute r;
    r.kind = AdminRoute::NoTicketId;
    return r;
}

}  // namespace deltafeedback::dc
