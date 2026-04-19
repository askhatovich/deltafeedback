#include "db/replay.h"
#include "db_fixture.h"

using deltafeedback::db::ReplayStore;
using deltafeedback::test_support::DbTest;

TEST_F(DbTest, ReplayFirstUseAccepted) {
    ReplayStore r(sql());
    EXPECT_TRUE(r.try_consume("\x01\x02\x03\x04", 1000));
}

TEST_F(DbTest, ReplaySecondUseRejected) {
    ReplayStore r(sql());
    std::string id = "id-bytes";
    EXPECT_TRUE(r.try_consume(id, 1000));
    EXPECT_FALSE(r.try_consume(id, 1000));  // same id again — replay
}

TEST_F(DbTest, ReplayDifferentIdsIndependent) {
    ReplayStore r(sql());
    EXPECT_TRUE(r.try_consume("a", 1000));
    EXPECT_TRUE(r.try_consume("b", 1000));
}

TEST_F(DbTest, ReplayPurgeExpired) {
    ReplayStore r(sql());
    r.try_consume("alpha", 100);
    r.try_consume("beta",  500);
    r.try_consume("gamma", 1000);

    int n = r.purge_expired(700);
    EXPECT_EQ(n, 2);  // alpha and beta gone, gamma stays

    // gamma still recorded (rejected)
    EXPECT_FALSE(r.try_consume("gamma", 1000));
    // alpha can be reused (was purged)
    EXPECT_TRUE(r.try_consume("alpha", 100));
}
