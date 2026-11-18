#include "normalize.h"
#include "parse.h"

#include <gtest/gtest.h>

#include <format>

using namespace schemasync;

TEST(MakeObjectName, FollowsPostgresConvention) {
    EXPECT_EQ(make_object_name("users", "", "pkey"), "users_pkey");
    EXPECT_EQ(make_object_name("orders", "user_id", "fkey"), "orders_user_id_fkey");
    EXPECT_EQ(make_object_name("t", "a_b", "key"), "t_a_b_key");
}

TEST(MakeObjectName, TruncatesLongerPartFirstTo63Bytes) {
    const std::string table(40, 't');
    const std::string column(40, 'c');
    const auto name = make_object_name(table, column, "fkey");
    EXPECT_EQ(name.size(), 63u);
    EXPECT_EQ(name, std::string(29, 't') + "_" + std::string(28, 'c') + "_fkey");
}

TEST(RenderType, RoundTripsTypmods) {
    EXPECT_EQ(render_type({"varchar", 259}), "varchar(255)");
    EXPECT_EQ(render_type({"numeric", ((12 << 16) | 4) + 4}), "numeric(12,4)");
    EXPECT_EQ(render_type({"timestamptz", 3}), "timestamptz(3)");
    EXPECT_EQ(render_type({"_int4", -1}), "int4[]");
}

struct TypeCase {
    const char* sql;
    const char* name;
    int typmod;
    const char* rendered;
};

class TypeNormalization : public ::testing::TestWithParam<TypeCase> {};

TEST_P(TypeNormalization, ParsesToCatalogForm) {
    const auto& p = GetParam();
    auto s = parse_string(std::format("CREATE TABLE t(c {})", p.sql));
    ASSERT_TRUE(s) << to_string(s.error());
    const auto& col = s->tables.at({"public", "t"}).columns[0];
    EXPECT_EQ(col.type.name, p.name);
    EXPECT_EQ(col.type.typmod, p.typmod);
    EXPECT_EQ(render_type(col.type), p.rendered);
}

INSTANTIATE_TEST_SUITE_P(
    Aliases, TypeNormalization,
    ::testing::Values(TypeCase{"int", "int4", -1, "int4"}, TypeCase{"integer", "int4", -1, "int4"},
                      TypeCase{"int4", "int4", -1, "int4"}, TypeCase{"bigint", "int8", -1, "int8"},
                      TypeCase{"smallint", "int2", -1, "int2"}, TypeCase{"varchar(255)", "varchar", 259, "varchar(255)"},
                      TypeCase{"character varying(10)", "varchar", 14, "varchar(10)"},
                      TypeCase{"varchar", "varchar", -1, "varchar"},
                      TypeCase{"numeric(10,2)", "numeric", 655366, "numeric(10,2)"},
                      TypeCase{"decimal(5)", "numeric", 327684, "numeric(5,0)"}, TypeCase{"numeric", "numeric", -1, "numeric"},
                      TypeCase{"text", "text", -1, "text"}, TypeCase{"boolean", "bool", -1, "bool"},
                      TypeCase{"timestamptz", "timestamptz", -1, "timestamptz"},
                      TypeCase{"timestamp with time zone", "timestamptz", -1, "timestamptz"},
                      TypeCase{"timestamp(3)", "timestamp", 3, "timestamp(3)"}, TypeCase{"char(2)", "bpchar", 6, "bpchar(2)"},
                      TypeCase{"uuid", "uuid", -1, "uuid"}, TypeCase{"jsonb", "jsonb", -1, "jsonb"},
                      TypeCase{"text[]", "_text", -1, "text[]"}, TypeCase{"double precision", "float8", -1, "float8"},
                      TypeCase{"real", "float4", -1, "float4"}, TypeCase{"date", "date", -1, "date"}));
