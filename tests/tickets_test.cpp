#include "db/tickets.h"
#include "db_fixture.h"

using deltafeedback::db::Ticket;
using deltafeedback::db::TicketsRepo;
using deltafeedback::db::TicketStatus;
using deltafeedback::test_support::DbTest;

namespace {

Ticket make_ticket(const std::string& id, std::int64_t created_at = 1000) {
    Ticket t;
    t.id = id;
    t.read_token_hash = std::string(32, '\xAA');
    t.name = "Иван";
    t.locale = "ru";
    t.status = TicketStatus::AwaitingAdmin;
    t.created_at = created_at;
    return t;
}

}  // namespace

TEST_F(DbTest, TicketInsertAndGet) {
    TicketsRepo r(sql());
    r.insert_new(make_ticket("A3F2K9"));

    auto got = r.get("A3F2K9");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->id, "A3F2K9");
    EXPECT_EQ(got->name, "Иван");
    EXPECT_EQ(got->status, TicketStatus::AwaitingAdmin);
    EXPECT_FALSE(got->closed_at.has_value());
}

TEST_F(DbTest, TicketStatusTransitions) {
    TicketsRepo r(sql());
    r.insert_new(make_ticket("AAAAAA"));

    r.set_status("AAAAAA", TicketStatus::AwaitingVisitor);
    EXPECT_EQ(r.get("AAAAAA")->status, TicketStatus::AwaitingVisitor);

    r.close("AAAAAA", 2000);
    auto closed = r.get("AAAAAA");
    EXPECT_EQ(closed->status, TicketStatus::Closed);
    EXPECT_EQ(closed->closed_at.value(), 2000);
}

TEST_F(DbTest, ClosedTicketIsImmutable) {
    TicketsRepo r(sql());
    r.insert_new(make_ticket("BBBBBB"));
    r.close("BBBBBB", 100);

    // Set/close on a closed ticket should be a no-op.
    r.set_status("BBBBBB", TicketStatus::AwaitingVisitor);
    r.close("BBBBBB", 999);

    auto t = r.get("BBBBBB");
    EXPECT_EQ(t->status, TicketStatus::Closed);
    EXPECT_EQ(t->closed_at.value(), 100);  // not 999
}

TEST_F(DbTest, ListOpenSkipsClosed) {
    TicketsRepo r(sql());
    r.insert_new(make_ticket("OPEN01", 100));
    r.insert_new(make_ticket("OPEN02", 200));
    r.insert_new(make_ticket("CLOSED", 300));
    r.close("CLOSED", 400);

    auto open = r.list_open();
    EXPECT_EQ(open.size(), 2u);
    // ORDER BY created_at DESC
    EXPECT_EQ(open[0].id, "OPEN02");
    EXPECT_EQ(open[1].id, "OPEN01");
}

TEST_F(DbTest, PurgeInactiveDeletesAbandonedAndStaleClosed) {
    TicketsRepo r(sql());
    r.insert_new(make_ticket("OLDONE", /*created_at=*/100));
    r.insert_new(make_ticket("NEWONE", /*created_at=*/1000));
    r.insert_new(make_ticket("ABANDN", /*created_at=*/200));
    r.close("OLDONE", 100);
    r.close("NEWONE", 1000);
    // ABANDN: open but admin never replied — last event = 200

    // cutoff = 700 → tickets with last event < 700 are purged.
    int n = r.purge_inactive_older_than(700);
    EXPECT_EQ(n, 2);
    EXPECT_FALSE(r.get("OLDONE").has_value());
    EXPECT_TRUE(r.get("NEWONE").has_value());
    EXPECT_FALSE(r.get("ABANDN").has_value());  // abandoned-by-admin path
}
