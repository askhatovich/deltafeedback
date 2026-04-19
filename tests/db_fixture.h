#pragma once

#include "db/database.h"

#include <gtest/gtest.h>

#include <memory>

namespace deltafeedback::test_support {

// Shared fixture: opens a fresh in-memory DB and applies the schema.
class DbTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_ = db::Database::open(":memory:");
    }

    sqlite3* sql() { return db_->handle(); }

    std::unique_ptr<db::Database> db_;
};

}  // namespace deltafeedback::test_support
