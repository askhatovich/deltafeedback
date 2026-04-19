#include "dc/admin_route.h"

#include <gtest/gtest.h>

#include <unordered_map>

using deltafeedback::dc::AdminRoute;
using deltafeedback::dc::route_admin_message;

namespace {

// Stub lookup that mimics a small messages table.
struct FakeLookup {
    std::unordered_map<std::uint32_t, std::string> map;
    std::optional<std::string> operator()(std::uint32_t id) const {
        auto it = map.find(id);
        if (it == map.end()) return std::nullopt;
        return it->second;
    }
};

}  // namespace

TEST(AdminRoute, QuoteResolvesToTicket_BodyIsAdminText) {
    FakeLookup lookup; lookup.map[12345] = "A3F2K9";
    auto r = route_admin_message("спасибо, посмотрю",
                                 12345, "[A3F2K9] исходник",
                                 lookup);
    EXPECT_EQ(r.kind,      AdminRoute::Reply);
    EXPECT_EQ(r.ticket_id, "A3F2K9");
    EXPECT_EQ(r.body,      "спасибо, посмотрю");
}

TEST(AdminRoute, QuoteCloseExact) {
    FakeLookup lookup; lookup.map[42] = "ZZZZZZ";
    auto r = route_admin_message("/close", 42, "irrelevant", lookup);
    EXPECT_EQ(r.kind,      AdminRoute::Close);
    EXPECT_EQ(r.ticket_id, "ZZZZZZ");
    EXPECT_TRUE(r.body.empty());
}

TEST(AdminRoute, QuoteWithFarewellTextIsReply) {
    FakeLookup lookup; lookup.map[7] = "QQQQQQ";
    auto r = route_admin_message("/close спасибо", 7, "", lookup);
    EXPECT_EQ(r.kind,      AdminRoute::Reply);
    EXPECT_EQ(r.body,      "/close спасибо");
}

TEST(AdminRoute, FallbackToTextIdWhenQuoteUnknown) {
    FakeLookup lookup;  // empty
    auto r = route_admin_message("[A3F2K9] hi", 99999, "", lookup);
    EXPECT_EQ(r.kind,      AdminRoute::Reply);
    EXPECT_EQ(r.ticket_id, "A3F2K9");
    EXPECT_EQ(r.body,      "hi");
}

TEST(AdminRoute, FallbackToQuotedTextId) {
    // No quoted_msg_id, no [ID] in body, but the quoted text contains one
    // (e.g. cross-device case where the quote text is preserved but its id
    // doesn't resolve locally).
    FakeLookup lookup;
    auto r = route_admin_message("посмотрел, ок",
                                 0, "[A3F2K9] исходник",
                                 lookup);
    EXPECT_EQ(r.kind,      AdminRoute::Reply);
    EXPECT_EQ(r.ticket_id, "A3F2K9");
    EXPECT_EQ(r.body,      "посмотрел, ок");
}

TEST(AdminRoute, QuotedTextCloseIsClose) {
    FakeLookup lookup;
    auto r = route_admin_message("/close", 0, "[A3F2K9] x", lookup);
    EXPECT_EQ(r.kind, AdminRoute::Close);
    EXPECT_EQ(r.ticket_id, "A3F2K9");
}

TEST(AdminRoute, NoTicketIdAnywhere) {
    FakeLookup lookup;
    auto r = route_admin_message("hello world", 0, "", lookup);
    EXPECT_EQ(r.kind, AdminRoute::NoTicketId);
}

TEST(AdminRoute, QuoteWinsOverIdInBody) {
    // Quote-based routing takes priority — even if the body also has an [ID],
    // the quoted_msg_id is the authoritative source.
    FakeLookup lookup; lookup.map[5] = "FROMQT";
    auto r = route_admin_message("[INBODY] text", 5, "", lookup);
    EXPECT_EQ(r.ticket_id, "FROMQT");
    EXPECT_EQ(r.body,      "[INBODY] text");  // not stripped
}
