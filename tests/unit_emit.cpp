#include "emit.h"
#include "parse.h"
#include "planner.h"
#include "test_util.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>

using namespace schemasync;

namespace {

QualifiedName qn(const char* name) { return {"public", name}; }

}  // namespace

class PlanGolden : public ::testing::TestWithParam<std::string> {};

TEST_P(PlanGolden, MatchesExpectedSql) {
    const auto dir = testutil::fixtures() / "plans" / GetParam();
    auto from = parse_string(testutil::read_file(dir / "from.sql"), "public", "from.sql");
    auto to = parse_string(testutil::read_file(dir / "to.sql"), "public", "to.sql");
    ASSERT_TRUE(from) << to_string(from.error());
    ASSERT_TRUE(to) << to_string(to.error());

    std::vector<std::string> warnings;
    auto changes = plan(diff(*from, *to, warnings));
    ASSERT_TRUE(changes) << changes.error().message;
    const auto sql = testutil::render_all(*changes);

    const auto golden = dir / "expected_plan.sql";
    if (std::getenv("SCHEMASYNC_UPDATE_GOLDEN")) std::ofstream(golden, std::ios::binary) << sql;
    EXPECT_EQ(sql, testutil::read_file(golden));
}

INSTANTIATE_TEST_SUITE_P(Fixtures, PlanGolden, ::testing::ValuesIn(testutil::plan_cases()),
                         [](const auto& info) { return info.param; });

TEST(Render, QuotesIdentifiersAndRendersSerial) {
    auto s = parse_string(R"(CREATE TABLE "User"(id serial primary key, "Name" varchar(20) not null default 'x'))");
    ASSERT_TRUE(s);
    EXPECT_EQ(render(CreateTable{s->tables.begin()->second}),
              "CREATE TABLE \"public\".\"User\" (\n"
              "    \"id\" serial NOT NULL,\n"
              "    \"Name\" varchar(20) NOT NULL DEFAULT 'x',\n"
              "    CONSTRAINT \"User_pkey\" PRIMARY KEY (\"id\")\n"
              ");");
}

TEST(Render, ForeignKeyActionsAndTypeChange) {
    ForeignKey fk{"p_u_fkey", {"u_id"}, qn("u"), {"id"}, 'c', 'n'};
    EXPECT_EQ(render(AddForeignKey{qn("p"), fk}),
              "ALTER TABLE \"public\".\"p\" ADD CONSTRAINT \"p_u_fkey\" FOREIGN KEY (\"u_id\") REFERENCES \"public\".\"u\" (\"id\")"
              " ON DELETE CASCADE ON UPDATE SET NULL;");
    EXPECT_EQ(render(AlterColumnType{qn("t"), "qty", {"int4"}, {"int8"}}),
              "ALTER TABLE \"public\".\"t\" ALTER COLUMN \"qty\" TYPE int8 USING \"qty\"::int8;");
}

TEST(Hazards, DropsAreDestructive) {
    EXPECT_TRUE(hazards(DropTable{qn("t")}) & hazard::destructive);
    EXPECT_TRUE(hazards(DropColumn{qn("t"), "a"}) & hazard::destructive);
    EXPECT_FALSE(hazards(DropIndex{}) & hazard::destructive);
}

TEST(Hazards, TypeChangesDistinguishWideningFromNarrowing) {
    auto change = [](TypeRef from, TypeRef to) { return hazards(AlterColumnType{qn("t"), "c", from, to}); };
    EXPECT_FALSE(change({"varchar", 54}, {"varchar", 104}) & (hazard::destructive | hazard::table_rewrite));
    EXPECT_FALSE(change({"varchar", 54}, {"text"}) & (hazard::destructive | hazard::table_rewrite));
    EXPECT_TRUE(change({"varchar", 104}, {"varchar", 54}) & hazard::destructive);
    EXPECT_TRUE(change({"text"}, {"int4"}) & hazard::destructive);

    const auto int_widening = change({"int4"}, {"int8"});
    EXPECT_FALSE(int_widening & hazard::destructive);
    EXPECT_TRUE(int_widening & hazard::table_rewrite);
    EXPECT_TRUE(change({"int8"}, {"int4"}) & hazard::destructive);

    const int n8_2 = ((8 << 16) | 2) + 4, n12_2 = ((12 << 16) | 2) + 4, n12_4 = ((12 << 16) | 4) + 4;
    EXPECT_FALSE(change({"numeric", n8_2}, {"numeric", n12_2}) & (hazard::destructive | hazard::table_rewrite));
    EXPECT_TRUE(change({"numeric", n12_4}, {"numeric", n8_2}) & hazard::destructive);
}

TEST(Hazards, VolatileDefaultRewritesTable) {
    auto add = [](const char* key) { return hazards(AddColumn{qn("t"), Column{"c", {"uuid"}, false, Default{key, key}}}); };
    EXPECT_TRUE(add("gen_random_uuid()") & hazard::table_rewrite);
    EXPECT_FALSE(add("=0") & hazard::table_rewrite);
    EXPECT_FALSE(add("CURRENT_TIMESTAMP") & hazard::table_rewrite);
    EXPECT_EQ(hazard_names(hazard::destructive | hazard::access_exclusive), "DESTRUCTIVE, ACCESS_EXCLUSIVE_LOCK");
}
