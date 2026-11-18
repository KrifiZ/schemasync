#include "diff.h"
#include "parse.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace schemasync;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

namespace {

QualifiedName qn(const char* name) { return {"public", name}; }

std::vector<Change> diff_sql(const std::string& from, const std::string& to, std::vector<std::string>* warnings = nullptr) {
    auto a = parse_string(from);
    auto b = parse_string(to);
    EXPECT_TRUE(a && b);
    std::vector<std::string> w;
    auto out = diff(*a, *b, w);
    if (warnings) *warnings = w;
    return out;
}

template <class T>
std::size_t count(const std::vector<Change>& changes) {
    return std::ranges::count_if(changes, [](const Change& c) { return std::holds_alternative<T>(c); });
}

}  // namespace

TEST(Diff, IdenticalSchemasProduceNoChanges) {
    const char* sql = "CREATE TABLE u(id serial primary key, email text unique); CREATE INDEX ON u (email);";
    EXPECT_THAT(diff_sql(sql, sql), IsEmpty());
}

TEST(Diff, NewTablesAreCreatedWithoutForeignKeys) {
    auto changes = diff_sql("", "CREATE TABLE u(id int primary key); CREATE TABLE p(id int primary key, u_id int references u);");
    EXPECT_EQ(count<CreateTable>(changes), 2u);
    EXPECT_EQ(count<AddForeignKey>(changes), 1u);
    for (const auto& c : changes) {
        if (const auto* ct = std::get_if<CreateTable>(&c)) {
            EXPECT_TRUE(ct->table.foreign_keys.empty());
        }
    }
}

TEST(Diff, ColumnChanges) {
    auto changes = diff_sql("CREATE TABLE t(id int primary key, a int, b text default 'x', c text, d text)",
                            "CREATE TABLE t(id int primary key, a bigint, b text, c text not null default 'y', e text)");
    EXPECT_THAT(changes, UnorderedElementsAre(Change{AlterColumnType{qn("t"), "a", {"int4"}, {"int8"}}},
                                              Change{DropDefault{qn("t"), "b"}},
                                              Change{SetDefault{qn("t"), "c", {"'y'", "=y"}}},
                                              Change{SetNotNull{qn("t"), "c"}},
                                              Change{AddColumn{qn("t"), Column{"e", {"text"}}}},
                                              Change{DropColumn{qn("t"), "d"}}));
}

TEST(Diff, EquivalentDefaultsProduceNoChange) {
    EXPECT_THAT(diff_sql("CREATE TABLE t(a timestamptz default now())", "CREATE TABLE t(a timestamptz default CURRENT_TIMESTAMP)"),
                IsEmpty());
}

TEST(Diff, RenameHeuristicWarnsButDoesNotRename) {
    std::vector<std::string> warnings;
    auto changes = diff_sql("CREATE TABLE t(id int, old_name text)", "CREATE TABLE t(id int, new_name text)", &warnings);
    EXPECT_EQ(count<AddColumn>(changes), 1u);
    EXPECT_EQ(count<DropColumn>(changes), 1u);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(warnings[0].find("RENAME COLUMN"), std::string::npos);
}

TEST(Diff, ChangedIndexIsDroppedAndRecreated) {
    auto changes = diff_sql("CREATE TABLE t(a int, b int); CREATE INDEX t_idx ON t (a);",
                            "CREATE TABLE t(a int, b int); CREATE INDEX t_idx ON t (a, b);");
    EXPECT_EQ(count<DropIndex>(changes), 1u);
    EXPECT_EQ(count<CreateIndex>(changes), 1u);
}

TEST(Diff, ChangedForeignKeyIsDroppedAndRecreated) {
    auto changes = diff_sql("CREATE TABLE u(id int primary key); CREATE TABLE p(u_id int references u);",
                            "CREATE TABLE u(id int primary key); CREATE TABLE p(u_id int references u on delete cascade);");
    EXPECT_EQ(count<DropConstraint>(changes), 1u);
    EXPECT_EQ(count<AddForeignKey>(changes), 1u);
}

TEST(Diff, DroppedTablesDropTheirForeignKeysExplicitly) {
    auto changes = diff_sql(
        "CREATE TABLE a(id int primary key, b_id int); CREATE TABLE b(id int primary key, a_id int references a);"
        "ALTER TABLE a ADD CONSTRAINT a_b_fk FOREIGN KEY (b_id) REFERENCES b;",
        "");
    EXPECT_EQ(count<DropConstraint>(changes), 2u);
    EXPECT_EQ(count<DropTable>(changes), 2u);
}

TEST(Diff, IndexesOfDroppedTablesAreNotDroppedSeparately) {
    auto changes = diff_sql("CREATE TABLE t(a int); CREATE INDEX ON t (a);", "");
    EXPECT_THAT(changes, UnorderedElementsAre(Change{DropTable{qn("t")}}));
}

TEST(Diff, PrimaryKeyChangeDropsAndAddsConstraint) {
    auto changes = diff_sql("CREATE TABLE t(a int primary key, b int)", "CREATE TABLE t(a int, b int, primary key (a, b))");
    EXPECT_THAT(changes, UnorderedElementsAre(Change{DropConstraint{qn("t"), "t_pkey", {"a"}, std::nullopt, {}}},
                                              Change{SetNotNull{qn("t"), "b"}},
                                              Change{AddPrimaryKey{qn("t"), {"t_pkey", {"a", "b"}}}}));
}
