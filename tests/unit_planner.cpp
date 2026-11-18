#include "diff.h"
#include "parse.h"
#include "planner.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

using namespace schemasync;

namespace {

std::vector<Change> planned(const std::string& from, const std::string& to) {
    auto a = parse_string(from);
    auto b = parse_string(to);
    EXPECT_TRUE(a && b);
    std::vector<std::string> warnings;
    auto p = plan(diff(*a, *b, warnings));
    EXPECT_TRUE(p) << (p ? "" : p.error().message);
    return p ? *p : std::vector<Change>{};
}

template <class T>
std::ptrdiff_t first(const std::vector<Change>& changes) {
    auto it = std::ranges::find_if(changes, [](const Change& c) { return std::holds_alternative<T>(c); });
    return it == changes.end() ? -1 : it - changes.begin();
}

template <class T>
std::ptrdiff_t last(const std::vector<Change>& changes) {
    for (auto i = static_cast<std::ptrdiff_t>(changes.size()) - 1; i >= 0; --i)
        if (std::holds_alternative<T>(changes[i])) return i;
    return -1;
}

}  // namespace

TEST(TopoSort, RespectsEdgesThenKeyOrder) {
    const std::vector<SortKey> keys{{1, 0, "c", 0}, {1, 0, "a", 1}, {1, 0, "b", 2}, {0, 0, "z", 3}};
    auto order = topo_sort(keys, {{2, 1}});
    ASSERT_TRUE(order);
    EXPECT_EQ(*order, (std::vector<std::size_t>{3, 2, 1, 0}));
}

TEST(TopoSort, ReturnsNodesStuckInCycle) {
    const std::vector<SortKey> keys{{1, 0, "a", 0}, {1, 0, "b", 1}, {1, 0, "c", 2}, {1, 0, "free", 3}};
    auto order = topo_sort(keys, {{0, 1}, {1, 2}, {2, 0}});
    ASSERT_FALSE(order);
    EXPECT_EQ(order.error(), (std::vector<std::size_t>{0, 1, 2}));
}

TEST(Plan, KeepsDeclaredColumnOrderForNewColumns) {
    auto changes = planned("CREATE TABLE t(id int)", "CREATE TABLE t(id int, zeta int, alpha int, mid int)");
    ASSERT_EQ(changes.size(), 3u);
    EXPECT_EQ(std::get<AddColumn>(changes[0]).column.name, "zeta");
    EXPECT_EQ(std::get<AddColumn>(changes[1]).column.name, "alpha");
    EXPECT_EQ(std::get<AddColumn>(changes[2]).column.name, "mid");
}

TEST(Plan, MutualAndSelfForeignKeysHaveNoCycle) {
    auto changes = planned("",
                           "CREATE TABLE a(id int primary key, b_id int);"
                           "CREATE TABLE b(id int primary key, a_id int references a);"
                           "ALTER TABLE a ADD CONSTRAINT a_b_fk FOREIGN KEY (b_id) REFERENCES b;"
                           "CREATE TABLE employees(id int primary key, manager_id int references employees);");
    ASSERT_EQ(changes.size(), 6u);
    EXPECT_LT(last<CreateTable>(changes), first<AddForeignKey>(changes));
}

TEST(Plan, IsDeterministicRegardlessOfInputOrder) {
    auto a = parse_string("");
    auto b = parse_string(
        "CREATE TABLE u(id int primary key, email text unique);"
        "CREATE TABLE p(id int primary key, u_id int references u, title text);"
        "CREATE INDEX ON p (title); CREATE INDEX ON p (u_id);");
    std::vector<std::string> warnings;
    auto changes = diff(*a, *b, warnings);
    auto expected = plan(changes);
    ASSERT_TRUE(expected);

    std::mt19937 rng(42);
    for (int i = 0; i < 10; ++i) {
        std::ranges::shuffle(changes, rng);
        EXPECT_EQ(plan(changes), expected);
    }
}

TEST(Plan, DropsForeignKeysBeforeTheReferencedTable) {
    auto changes = planned("CREATE TABLE authors(id int primary key); CREATE TABLE books(id int, author_id int references authors);",
                           "CREATE TABLE books(id int, author_id int);");
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<DropConstraint>(changes[0]));
    EXPECT_TRUE(std::holds_alternative<DropTable>(changes[1]));
}

TEST(Plan, DropsIndexBeforeItsColumn) {
    auto changes = planned("CREATE TABLE t(id int, a int); CREATE INDEX ON t (a);", "CREATE TABLE t(id int);");
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<DropIndex>(changes[0]));
    EXPECT_TRUE(std::holds_alternative<DropColumn>(changes[1]));
}

TEST(Plan, AddsUniqueKeyBeforeForeignKeyThatNeedsIt) {
    auto changes = planned("CREATE TABLE u(id int primary key, code text); CREATE TABLE p(id int);",
                           "CREATE TABLE u(id int primary key, code text unique); CREATE TABLE p(id int, code text references u(code));");
    EXPECT_LT(first<AddColumn>(changes), first<AddForeignKey>(changes));
    EXPECT_LT(first<AddUnique>(changes), first<AddForeignKey>(changes));
}

TEST(Plan, SetsDefaultBeforeNotNull) {
    auto changes = planned("CREATE TABLE t(a int)", "CREATE TABLE t(a int not null default 0)");
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<SetDefault>(changes[0]));
    EXPECT_TRUE(std::holds_alternative<SetNotNull>(changes[1]));
}
