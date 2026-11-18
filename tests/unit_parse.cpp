#include "parse.h"
#include "test_util.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <pg_query.h>

#include <cstdlib>
#include <fstream>

using namespace schemasync;

namespace {

nlohmann::json parse_to_json(const char* sql) {
    PgQueryParseResult r = pg_query_parse(sql);
    EXPECT_EQ(r.error, nullptr) << (r.error ? r.error->message : "");
    auto j = nlohmann::json::parse(r.parse_tree ? r.parse_tree : "null");
    pg_query_free_parse_result(r);
    return j;
}

Schema parse_ok(const std::string& sql) {
    auto s = parse_string(sql);
    EXPECT_TRUE(s) << (s ? "" : to_string(s.error()));
    return s ? *s : Schema{};
}

const Table& table(const Schema& s, const char* name) { return s.tables.at({"public", name}); }

}  // namespace

TEST(LibPgQuery, CreateTableAstMatchesGolden) {
    const auto ast = parse_to_json("CREATE TABLE t(id int primary key)");
    const auto golden = testutil::fixtures() / "ast" / "create_table.json";
    if (std::getenv("SCHEMASYNC_UPDATE_GOLDEN")) std::ofstream(golden, std::ios::binary) << ast.dump(2) << '\n';
    EXPECT_EQ(ast, nlohmann::json::parse(testutil::read_file(golden)));
}

TEST(LibPgQuery, IntIsCanonicalizedToInt4) {
    const auto ast = parse_to_json("CREATE TABLE t(id int)");
    const auto& names = ast.at("stmts")[0].at("stmt").at("CreateStmt").at("tableElts")[0].at("ColumnDef").at("typeName").at("names");
    EXPECT_EQ(names[0].at("String").at("sval"), "pg_catalog");
    EXPECT_EQ(names[1].at("String").at("sval"), "int4");
}

TEST(LibPgQuery, SyntaxErrorHasCursorPosition) {
    PgQueryParseResult r = pg_query_parse("CREATE TABL t(id int)");
    ASSERT_NE(r.error, nullptr);
    EXPECT_EQ(r.error->cursorpos, 8);
    pg_query_free_parse_result(r);
}

TEST(Parse, InlineAndTableLevelForeignKeysAreEquivalent) {
    auto inline_fk = parse_ok(
        "CREATE TABLE u(id int primary key);"
        "CREATE TABLE p(id int primary key, u_id int references u(id) on delete cascade);");
    auto table_fk = parse_ok(
        "CREATE TABLE u(id int, primary key (id));"
        "CREATE TABLE p(id int primary key, u_id int, foreign key (u_id) references u on delete cascade);");
    EXPECT_EQ(inline_fk, table_fk);

    const auto& fk = table(inline_fk, "p").foreign_keys.at("p_u_id_fkey");
    EXPECT_EQ(fk.ref_columns, std::vector<std::string>{"id"});
    EXPECT_EQ(fk.on_delete, 'c');
    EXPECT_EQ(fk.on_update, 'a');
}

TEST(Parse, AlterTableAddConstraintMatchesInline) {
    auto inline_keys = parse_ok("CREATE TABLE u(id int primary key, email text unique);");
    auto altered = parse_ok(
        "CREATE TABLE u(id int, email text);"
        "ALTER TABLE ONLY public.u ADD CONSTRAINT u_pkey PRIMARY KEY (id);"
        "ALTER TABLE u ADD CONSTRAINT u_email_key UNIQUE (email);");
    EXPECT_EQ(inline_keys, altered);
}

TEST(Parse, PrimaryKeyImpliesNotNull) {
    auto s = parse_ok("CREATE TABLE t(a int, b int, primary key (a, b))");
    EXPECT_TRUE(table(s, "t").columns[0].not_null);
    EXPECT_TRUE(table(s, "t").columns[1].not_null);
}

TEST(Parse, SerialExpandsToIntegerWithSequenceDefault) {
    auto s = parse_ok("CREATE TABLE t(id serial primary key, big bigserial)");
    const auto& id = table(s, "t").columns[0];
    EXPECT_EQ(id.type, (TypeRef{"int4"}));
    EXPECT_TRUE(id.not_null);
    EXPECT_EQ(id.default_value->sql, "nextval('t_id_seq'::regclass)");
    const auto& big = table(s, "t").columns[1];
    EXPECT_EQ(big.type, (TypeRef{"int8"}));
    EXPECT_EQ(big.default_value->key, "nextval(=t_big_seq)");
}

TEST(Parse, EquivalentDefaultsCompareEqual) {
    auto a = parse_ok("CREATE TABLE t(a timestamptz default now(), b varchar(5) default 'x', c int default -1, d text default null)");
    auto b = parse_ok(
        "CREATE TABLE t(a timestamptz default CURRENT_TIMESTAMP, b varchar(5) default 'x'::character varying,"
        " c int default '-1'::integer, d text)");
    EXPECT_EQ(a, b);
    EXPECT_FALSE(table(a, "t").columns[3].default_value);
}

TEST(Parse, CatalogDefaultTextMatchesSourceDefault) {
    auto s = parse_ok("CREATE TABLE t(id serial, b varchar(5) default 'x', c boolean default false, d uuid default gen_random_uuid())");
    const auto& cols = table(s, "t").columns;
    EXPECT_EQ(parse_default_expr("nextval('t_id_seq'::regclass)"), cols[0].default_value);
    EXPECT_EQ(parse_default_expr("'x'::character varying"), cols[1].default_value);
    EXPECT_EQ(parse_default_expr("false"), cols[2].default_value);
    EXPECT_EQ(parse_default_expr("gen_random_uuid()"), cols[3].default_value);
    EXPECT_EQ(parse_default_expr("NULL::text"), std::nullopt);
}

TEST(Parse, UnparseableCatalogDefaultFallsBackToText) {
    auto d = parse_default_expr("(a + 1)");
    ASSERT_TRUE(d);
    EXPECT_EQ(d->key, "(a + 1)");
}

TEST(Parse, GeneratesPostgresConstraintAndIndexNames) {
    auto s = parse_ok(
        "CREATE TABLE t(id int primary key, a int, b int, c int references t, unique (a, b));"
        "CREATE INDEX ON t (a);"
        "CREATE UNIQUE INDEX ON t (b, a);");
    const auto& t = table(s, "t");
    EXPECT_EQ(t.primary_key->name, "t_pkey");
    EXPECT_TRUE(t.uniques.contains("t_a_b_key"));
    EXPECT_TRUE(t.foreign_keys.contains("t_c_fkey"));
    EXPECT_TRUE(s.indexes.contains({"public", "t_a_idx"}));
    EXPECT_TRUE(s.indexes.at({"public", "t_b_a_idx"}).unique);
}

TEST(Parse, UnsupportedStatementReportsFileLineAndColumn) {
    auto s = parse_string("CREATE TABLE t(id int);\n\n  -- a view\n  CREATE VIEW v AS SELECT 1;", "public", "schema.sql");
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error().kind, ErrorKind::unsupported);
    EXPECT_EQ(s.error().line, 4);
    EXPECT_EQ(s.error().col, 3);
    EXPECT_TRUE(to_string(s.error()).starts_with("schema.sql:4:3: unsupported statement ViewStmt"));
}

TEST(Parse, SyntaxErrorReportsLineAndColumn) {
    auto s = parse_string("CREATE TABLE t(id int);\nCREATE TABL x();", "public", "users.sql");
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error().kind, ErrorKind::parse);
    EXPECT_EQ(s.error().line, 2);
    EXPECT_EQ(s.error().col, 8);
    EXPECT_EQ(to_string(s.error()), "users.sql:2:8: syntax error at or near \"TABL\"");
}

TEST(Parse, RejectsUnsupportedFeatures) {
    for (const char* sql : {
             "CREATE TABLE t(a int check (a > 0))",
             "CREATE TABLE t(a int generated always as identity)",
             "CREATE TABLE t(a text collate \"C\")",
             "CREATE TABLE t(a int default 1 + 1)",
             "CREATE TABLE other.t(a int)",
             "CREATE TEMP TABLE t(a int)",
             "CREATE TABLE t(a int); CREATE INDEX ON t ((a + 1))",
             "CREATE TABLE t(a int); CREATE INDEX ON t (a) WHERE a > 0",
             "CREATE TABLE t(a int); CREATE INDEX ON t (a DESC)",
             "CREATE TABLE t(a int); CREATE INDEX ON t USING hash (a)",
             "CREATE TABLE t(a int); ALTER TABLE t ADD COLUMN b int",
             "CREATE SEQUENCE s",
         }) {
        EXPECT_FALSE(parse_string(sql)) << sql;
    }
}

TEST(Parse, RejectsUnknownReferences) {
    for (const char* sql : {
             "CREATE TABLE t(a int references missing)",
             "CREATE TABLE u(id int); CREATE TABLE t(a int references u)",
             "CREATE TABLE t(a int, primary key (b))",
             "CREATE TABLE t(a int); CREATE INDEX ON t (b)",
             "CREATE INDEX ON missing (a)",
             "ALTER TABLE missing ADD CONSTRAINT x UNIQUE (a)",
             "CREATE TABLE t(a int); CREATE TABLE t(b int)",
         }) {
        EXPECT_FALSE(parse_string(sql)) << sql;
    }
}

TEST(Parse, LoadsFilesInAlphabeticalOrder) {
    auto s = load_schema_files(testutil::fixtures() / "files", "public");
    ASSERT_TRUE(s) << to_string(s.error());
    EXPECT_EQ(s->tables.size(), 2u);
    EXPECT_TRUE(table(*s, "posts").foreign_keys.contains("posts_author_id_fkey"));
}

TEST(Parse, LoadErrorNamesTheFile) {
    auto s = load_schema_files(testutil::fixtures() / "files_invalid", "public");
    ASSERT_FALSE(s);
    EXPECT_TRUE(s.error().file.ends_with("bad.sql")) << s.error().file;
    EXPECT_EQ(s.error().line, 3);
}
