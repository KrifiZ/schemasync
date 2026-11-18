#include "introspect.h"
#include "parse.h"
#include "test_util.h"

#include <gtest/gtest.h>

using namespace schemasync;
using testutil::TempDbTest;

class IntrospectTest : public TempDbTest {
protected:
    Schema introspect_public(std::vector<std::string>& warnings) {
        auto tx = db::Transaction::begin(conn(), "ISOLATION LEVEL REPEATABLE READ");
        EXPECT_TRUE(tx);
        auto s = introspect(conn(), "public", warnings);
        EXPECT_TRUE(s) << (s ? "" : to_string(s.error()));
        return s ? *s : Schema{};
    }
};

class IntrospectMatchesParse : public IntrospectTest, public ::testing::WithParamInterface<std::string> {};

TEST_P(IntrospectMatchesParse, ForEveryFixtureSchema) {
    for (const char* file : {"from.sql", "to.sql"}) {
        SCOPED_TRACE(file);
        exec_ok("DROP SCHEMA public CASCADE; CREATE SCHEMA public;");
        const auto sql = testutil::read_file(testutil::fixtures() / "plans" / GetParam() / file);
        exec_ok(sql);
        auto expected = parse_string(sql);
        ASSERT_TRUE(expected) << to_string(expected.error());

        std::vector<std::string> warnings;
        const auto actual = introspect_public(warnings);
        EXPECT_EQ(testutil::describe_diff(actual, *expected), "");
        EXPECT_TRUE(actual == *expected);
        EXPECT_TRUE(warnings.empty());
    }
}

INSTANTIATE_TEST_SUITE_P(Fixtures, IntrospectMatchesParse, ::testing::ValuesIn(testutil::plan_cases()),
                         [](const auto& info) { return info.param; });

TEST_F(IntrospectTest, WarnsAboutObjectsItDoesNotManage) {
    exec_ok("CREATE TABLE t(a int CHECK (a > 0)); CREATE INDEX t_expr ON t ((a + 1)); CREATE INDEX t_part ON t (a) WHERE a > 5;");
    std::vector<std::string> warnings;
    const auto s = introspect_public(warnings);
    EXPECT_EQ(warnings.size(), 3u);
    EXPECT_TRUE(s.indexes.empty());
}

TEST_F(IntrospectTest, IgnoresOtherSchemasAndExtensionTables) {
    exec_ok("CREATE SCHEMA other; CREATE TABLE other.x(a int); CREATE TABLE public.y(a int);");
    std::vector<std::string> warnings;
    const auto s = introspect_public(warnings);
    ASSERT_EQ(s.tables.size(), 1u);
    EXPECT_EQ(s.tables.begin()->first.name, "y");
}

TEST_F(IntrospectTest, RejectsIdentityColumns) {
    exec_ok("CREATE TABLE t(id int GENERATED ALWAYS AS IDENTITY)");
    auto tx = db::Transaction::begin(conn());
    std::vector<std::string> warnings;
    auto s = introspect(conn(), "public", warnings);
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error().kind, ErrorKind::unsupported);
}
