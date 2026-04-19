#include "db/statement.h"

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <memory>

using deltafeedback::db::Statement;

namespace {

struct DbHolder {
    sqlite3* db = nullptr;
    DbHolder() {
        if (sqlite3_open(":memory:", &db) != SQLITE_OK) std::abort();
        char* err = nullptr;
        if (sqlite3_exec(db,
                "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, n INTEGER);",
                nullptr, nullptr, &err) != SQLITE_OK) {
            sqlite3_free(err);
            std::abort();
        }
    }
    ~DbHolder() { sqlite3_close(db); }
};

}  // namespace

TEST(Statement, BindAndStep) {
    DbHolder h;
    {
        Statement ins(h.db, "INSERT INTO t(name, n) VALUES(?1, ?2)");
        ins.bind_text(1, "alice");
        ins.bind_int(2, 42);
        EXPECT_FALSE(ins.step());  // INSERT yields no row
    }
    Statement sel(h.db, "SELECT name, n FROM t WHERE n = ?1");
    sel.bind_int(1, 42);
    ASSERT_TRUE(sel.step());
    EXPECT_EQ(sel.col_text(0), "alice");
    EXPECT_EQ(sel.col_int(1), 42);
    EXPECT_FALSE(sel.step());  // no more rows
}

TEST(Statement, ReusableAfterReset) {
    DbHolder h;
    Statement ins(h.db, "INSERT INTO t(name, n) VALUES(?1, ?2)");
    for (int i = 0; i < 3; ++i) {
        ins.reset();
        ins.bind_text(1, "row");
        ins.bind_int(2, i);
        EXPECT_FALSE(ins.step());
    }
    Statement count(h.db, "SELECT COUNT(*) FROM t");
    ASSERT_TRUE(count.step());
    EXPECT_EQ(count.col_int(0), 3);
}

TEST(Statement, RejectsRawConcatPattern) {
    // This test exists as documentation more than verification: any attempt to
    // build a Statement from a non-literal string still compiles (we can't
    // forbid that at the type level), but reviewers must reject such code.
    // The purpose of having all SQL in db/queries.h is to make this discipline
    // mechanical rather than vigilance-dependent.
    SUCCEED();
}

TEST(Statement, BindNullColumnReadsAsNull) {
    DbHolder h;
    {
        Statement ins(h.db, "INSERT INTO t(name, n) VALUES(?1, ?2)");
        ins.bind_text(1, "bob");
        ins.bind_null(2);
        EXPECT_FALSE(ins.step());
    }
    Statement sel(h.db, "SELECT n FROM t WHERE name = ?1");
    sel.bind_text(1, "bob");
    ASSERT_TRUE(sel.step());
    EXPECT_TRUE(sel.col_is_null(0));
}
