#include "db/state.h"
#include "db_fixture.h"

using deltafeedback::db::StateRepo;
using deltafeedback::test_support::DbTest;

TEST_F(DbTest, StateGetMissing) {
    StateRepo s(sql());
    EXPECT_FALSE(s.get("missing").has_value());
}

TEST_F(DbTest, StateSetAndGet) {
    StateRepo s(sql());
    s.set("k", "v1");
    EXPECT_EQ(s.get("k").value(), "v1");
}

TEST_F(DbTest, StateUpsert) {
    StateRepo s(sql());
    s.set("k", "v1");
    s.set("k", "v2");
    EXPECT_EQ(s.get("k").value(), "v2");
}

TEST_F(DbTest, StateErase) {
    StateRepo s(sql());
    s.set("k", "v");
    s.erase("k");
    EXPECT_FALSE(s.get("k").has_value());
}
