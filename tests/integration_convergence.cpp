#include "introspect.h"
#include "parse.h"
#include "sync.h"
#include "test_util.h"

#include <gtest/gtest.h>

using namespace schemasync;
using testutil::TempDbTest;

class Convergence : public TempDbTest, public ::testing::WithParamInterface<std::string> {};

TEST_P(Convergence, ApplyReachesDesiredStateAndSecondPlanIsEmpty) {
    const auto dir = testutil::fixtures() / "plans" / GetParam();
    exec_ok(testutil::read_file(dir / "from.sql"));
    auto desired = parse_string(testutil::read_file(dir / "to.sql"));
    ASSERT_TRUE(desired) << to_string(desired.error());

    Options options;
    options.allow_destructive = true;
    auto applied = apply_changes(conn(), *desired, options);
    ASSERT_TRUE(applied) << to_string(applied.error());

    auto again = plan_changes(conn(), *desired, options);
    ASSERT_TRUE(again) << to_string(again.error());
    EXPECT_EQ(testutil::render_all(again->changes), "");

    auto tx = db::Transaction::begin(conn());
    std::vector<std::string> warnings;
    auto current = introspect(conn(), "public", warnings);
    ASSERT_TRUE(current);
    EXPECT_TRUE(*current == *desired);
}

INSTANTIATE_TEST_SUITE_P(Fixtures, Convergence, ::testing::ValuesIn(testutil::plan_cases()),
                         [](const auto& info) { return info.param; });

using ApplyTest = TempDbTest;

TEST_F(ApplyTest, DryRunExecutesThenRollsBack) {
    exec_ok("CREATE TABLE t(id int PRIMARY KEY)");
    auto desired = parse_string("CREATE TABLE t(id int PRIMARY KEY, a text)");
    Options options;
    options.dry_run = true;
    auto r = apply_changes(conn(), *desired, options);
    ASSERT_TRUE(r) << to_string(r.error());
    EXPECT_EQ(r->changes.size(), 1u);
    EXPECT_EQ(plan_changes(conn(), *desired, options)->changes.size(), 1u);
}

TEST_F(ApplyTest, DryRunCatchesErrorsStaticPlanCannotSee) {
    exec_ok("CREATE TABLE t(id int, a text); INSERT INTO t VALUES (1, NULL);");
    auto desired = parse_string("CREATE TABLE t(id int, a text NOT NULL)");
    Options options;
    options.dry_run = true;
    auto r = apply_changes(conn(), *desired, options);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().sqlstate, "23502");
}

TEST_F(ApplyTest, RefusesDestructiveChangesWithoutFlag) {
    exec_ok("CREATE TABLE t(id int PRIMARY KEY, a text)");
    auto desired = parse_string("CREATE TABLE t(id int PRIMARY KEY)");
    auto r = apply_changes(conn(), *desired, Options{});
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().kind, ErrorKind::refused);
    EXPECT_NE(r.error().message.find("DROP COLUMN \"a\""), std::string::npos);
    EXPECT_EQ(plan_changes(conn(), *desired, Options{})->changes.size(), 1u);
}

TEST_F(ApplyTest, FailedStatementRollsBackWholePlan) {
    exec_ok("CREATE TABLE t(id int, a text); INSERT INTO t VALUES (1, NULL);");
    auto desired = parse_string("CREATE TABLE t(id int, a text NOT NULL, b int); CREATE TABLE u(id int);");
    auto r = apply_changes(conn(), *desired, Options{});
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().message.find("while executing"), std::string::npos);
    EXPECT_EQ(plan_changes(conn(), *desired, Options{})->changes.size(), 3u);
}

TEST_F(ApplyTest, LockTimeoutAbortsInsteadOfQueueing) {
    exec_ok("CREATE TABLE t(id int PRIMARY KEY)");
    auto other = db::Connection::open(dsn_for_db());
    ASSERT_TRUE(other);
    ASSERT_TRUE(other->exec("BEGIN; LOCK TABLE t IN ACCESS EXCLUSIVE MODE;"));

    auto desired = parse_string("CREATE TABLE t(id int PRIMARY KEY, a text)");
    Options options;
    options.lock_timeout = "200ms";
    auto r = apply_changes(conn(), *desired, options);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().sqlstate, "55P03");
    (void)other->exec("ROLLBACK");
}
