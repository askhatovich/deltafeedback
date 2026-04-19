#include "db/messages.h"
#include "db/tickets.h"
#include "db_fixture.h"

using deltafeedback::db::MessagesRepo;
using deltafeedback::db::Sender;
using deltafeedback::db::Ticket;
using deltafeedback::db::TicketsRepo;
using deltafeedback::db::TicketStatus;
using deltafeedback::test_support::DbTest;

namespace {
Ticket make_ticket(const std::string& id) {
    Ticket t;
    t.id = id;
    t.read_token_hash = std::string(32, '\xBB');
    t.locale = "ru";
    t.status = TicketStatus::AwaitingAdmin;
    t.created_at = 1000;
    return t;
}
}  // namespace

TEST_F(DbTest, MessagesAppendAndList) {
    TicketsRepo  tr(sql());
    MessagesRepo mr(sql());
    tr.insert_new(make_ticket("AAAAAA"));

    mr.append("AAAAAA", Sender::Visitor, "first", 100, "192.168.1.10");
    mr.append("AAAAAA", Sender::Admin,   "reply", 200, "");
    mr.append("AAAAAA", Sender::Visitor, "again", 300, "192.168.1.11");

    auto v = mr.list("AAAAAA");
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0].sender, Sender::Visitor);
    EXPECT_EQ(v[0].body, "first");
    EXPECT_EQ(v[0].created_at, 100);
    EXPECT_EQ(v[1].sender, Sender::Admin);
    EXPECT_EQ(v[1].body, "reply");
    EXPECT_EQ(v[2].sender, Sender::Visitor);
    EXPECT_EQ(v[2].body, "again");
}

TEST_F(DbTest, MessagesDcMsgIdRoundtrip) {
    TicketsRepo  tr(sql());
    MessagesRepo mr(sql());
    tr.insert_new(make_ticket("DCMID1"));

    auto row1 = mr.append("DCMID1", Sender::Visitor, "first", 100, "1.2.3.4", 7777u);
    auto row2 = mr.append("DCMID1", Sender::Visitor, "second", 200, "1.2.3.4");
    EXPECT_GT(row1, 0);
    EXPECT_GT(row2, 0);

    EXPECT_EQ(mr.find_ticket_by_dc_msg_id(7777).value(), "DCMID1");
    EXPECT_FALSE(mr.find_ticket_by_dc_msg_id(8888).has_value());

    mr.set_dc_msg_id(row2, 8888);
    EXPECT_EQ(mr.find_ticket_by_dc_msg_id(8888).value(), "DCMID1");
}

TEST_F(DbTest, LatestDcMsgIdForTicket) {
    TicketsRepo  tr(sql());
    MessagesRepo mr(sql());
    tr.insert_new(make_ticket("THREAD"));

    EXPECT_FALSE(mr.latest_dc_msg_id_for_ticket("THREAD").has_value());

    mr.append("THREAD", Sender::Visitor, "first",  100, "1.1.1.1", 1001u);
    mr.append("THREAD", Sender::Admin,   "reply",  150, "");        // admin row, NULL
    mr.append("THREAD", Sender::Visitor, "second", 200, "1.1.1.1", 1002u);

    EXPECT_EQ(mr.latest_dc_msg_id_for_ticket("THREAD").value(), 1002u);
}

TEST_F(DbTest, MessagesCascadeOnTicketPurge) {
    TicketsRepo  tr(sql());
    MessagesRepo mr(sql());
    Ticket t = make_ticket("CASCAD"); t.created_at = 100;
    tr.insert_new(t);
    mr.append("CASCAD", Sender::Visitor, "x", 100, "1.2.3.4");
    mr.append("CASCAD", Sender::Admin,   "y", 110, "");
    tr.close("CASCAD", 200);

    // Latest event = 200 (closed_at). Purge with cutoff 500 → deleted.
    int n = tr.purge_inactive_older_than(500);
    EXPECT_EQ(n, 1);
    EXPECT_TRUE(mr.list("CASCAD").empty());
}

TEST_F(DbTest, RecentMessageKeepsTicketAlive) {
    TicketsRepo  tr(sql());
    MessagesRepo mr(sql());
    Ticket t = make_ticket("FRESH"); t.created_at = 100;
    tr.insert_new(t);
    mr.append("FRESH", Sender::Visitor, "old",    100, "1.2.3.4");
    mr.append("FRESH", Sender::Visitor, "recent", 1000, "1.2.3.4");
    // Open ticket; latest event = 1000 (most recent message). Cutoff 500
    // would have purged it under "created_at"-only logic but keeps it now.
    int n = tr.purge_inactive_older_than(500);
    EXPECT_EQ(n, 0);
    EXPECT_TRUE(tr.get("FRESH").has_value());
}
