#pragma once

#include <string>
#include <string_view>

namespace deltafeedback::dc {

struct AdminCommand {
    enum Kind {
        Reply,        // text non-empty, ticket_id valid
        Close,        // command was exactly "/close"
        NoTicketId,   // text didn't contain [XXXXXX]
    };
    Kind        kind = NoTicketId;
    std::string ticket_id;
    std::string body;          // for Reply: stripped of [ID] prefix and trim
};

// Pure parser: extracted from Bot so it can be unit-tested without DC FFI.
//
//   - Finds the FIRST occurrence of [A-Z0-9]{6} in the text → ticket id.
//   - Strips that token, trims surrounding whitespace.
//   - If the result is exactly "/close" → Close. Otherwise → Reply.
AdminCommand parse_admin_message(std::string_view text);

}  // namespace deltafeedback::dc
