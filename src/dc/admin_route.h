#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace deltafeedback::dc {

// Result of routing an admin message to a ticket.
struct AdminRoute {
    enum Kind { Reply, Close, NoTicketId };
    Kind        kind = NoTicketId;
    std::string ticket_id;
    std::string body;          // empty for Close
};

// Pure routing function. Tries three sources, in order:
//   1. quoted_msg_id is non-zero AND the lookup returns a ticket id
//      → use that ticket; body = trim(text).
//   2. text contains [A-Z0-9]{6} (admin typed ID manually)
//      → strip the marker, body = remainder trimmed.
//   3. quoted_text contains [A-Z0-9]{6} (cross-device case: quote text
//      preserved but its msg id doesn't resolve in our DB)
//      → use the quoted ID; body = trim(text) — admin's typed text only.
//
// If body equals exactly "/close" it becomes Close; otherwise Reply.
//
// `lookup` lets callers inject the messages-table reverse lookup (or a stub
// for tests). It returns the ticket_id for a given outbound DC msg id.
AdminRoute route_admin_message(
    std::string_view text,
    std::uint32_t    quoted_msg_id,
    std::string_view quoted_text,
    const std::function<std::optional<std::string>(std::uint32_t)>& lookup);

}  // namespace deltafeedback::dc
