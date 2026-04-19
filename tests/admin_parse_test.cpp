#include "dc/admin_parse.h"

#include <gtest/gtest.h>

using deltafeedback::dc::AdminCommand;
using deltafeedback::dc::parse_admin_message;

TEST(AdminParse, NoTicketId) {
    auto c = parse_admin_message("plain text without any marker");
    EXPECT_EQ(c.kind, AdminCommand::NoTicketId);
    EXPECT_TRUE(c.ticket_id.empty());
    EXPECT_TRUE(c.body.empty());
}

TEST(AdminParse, IdAtStart_Reply) {
    auto c = parse_admin_message("[A3F2K9] спасибо, посмотрю");
    EXPECT_EQ(c.kind, AdminCommand::Reply);
    EXPECT_EQ(c.ticket_id, "A3F2K9");
    EXPECT_EQ(c.body, "спасибо, посмотрю");
}

TEST(AdminParse, IdInTheMiddle) {
    auto c = parse_admin_message("> цитата\n[A3F2K9] здравствуйте\n> ещё");
    EXPECT_EQ(c.kind, AdminCommand::Reply);
    EXPECT_EQ(c.ticket_id, "A3F2K9");
    EXPECT_EQ(c.body, "> цитата\n здравствуйте\n> ещё");
}

TEST(AdminParse, FirstIdWins) {
    auto c = parse_admin_message("[ABCDEF] hello [QQQQQQ]");
    EXPECT_EQ(c.ticket_id, "ABCDEF");
}

TEST(AdminParse, CloseExact) {
    auto c = parse_admin_message("[ABCDEF] /close");
    EXPECT_EQ(c.kind, AdminCommand::Close);
    EXPECT_EQ(c.ticket_id, "ABCDEF");
    EXPECT_TRUE(c.body.empty());
}

TEST(AdminParse, CloseWithExtraTextIsReply) {
    // "/close спасибо" is NOT a close command — it's a normal reply.
    auto c = parse_admin_message("[ABCDEF] /close спасибо");
    EXPECT_EQ(c.kind, AdminCommand::Reply);
    EXPECT_EQ(c.body, "/close спасибо");
}

TEST(AdminParse, RejectsLowercaseId) {
    auto c = parse_admin_message("[abcdef] hi");
    EXPECT_EQ(c.kind, AdminCommand::NoTicketId);
}

TEST(AdminParse, RejectsWrongLength) {
    EXPECT_EQ(parse_admin_message("[ABCDE] x").kind,  AdminCommand::NoTicketId);
    EXPECT_EQ(parse_admin_message("[ABCDEFG] x").kind, AdminCommand::NoTicketId);
}

TEST(AdminParse, BodyTrimmed) {
    auto c = parse_admin_message("[ABCDEF]   hello   ");
    EXPECT_EQ(c.body, "hello");
}
