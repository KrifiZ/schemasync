#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <pg_query.h>

#include <cstdlib>
#include <fstream>

namespace {

nlohmann::json parse_to_json(const char* sql) {
    PgQueryParseResult r = pg_query_parse(sql);
    EXPECT_EQ(r.error, nullptr) << (r.error ? r.error->message : "");
    auto j = nlohmann::json::parse(r.parse_tree ? r.parse_tree : "null");
    pg_query_free_parse_result(r);
    return j;
}

}  // namespace

TEST(LibPgQuery, CreateTableAstMatchesGolden) {
    const auto ast = parse_to_json("CREATE TABLE t(id int primary key)");
    const std::string golden = SCHEMASYNC_FIXTURES "/ast/create_table.json";

    if (std::getenv("SCHEMASYNC_UPDATE_GOLDEN")) {
        std::ofstream(golden) << ast.dump(2) << '\n';
    }
    std::ifstream in(golden);
    ASSERT_TRUE(in) << "missing " << golden << ", run with SCHEMASYNC_UPDATE_GOLDEN=1";
    EXPECT_EQ(ast, nlohmann::json::parse(in));
}

TEST(LibPgQuery, IntIsCanonicalizedToInt4) {
    const auto ast = parse_to_json("CREATE TABLE t(id int)");
    const auto& names = ast.at("stmts")[0].at("stmt").at("CreateStmt").at("tableElts")[0]
                            .at("ColumnDef").at("typeName").at("names");
    EXPECT_EQ(names[0].at("String").at("sval"), "pg_catalog");
    EXPECT_EQ(names[1].at("String").at("sval"), "int4");
}

TEST(LibPgQuery, SyntaxErrorHasCursorPosition) {
    PgQueryParseResult r = pg_query_parse("CREATE TABL t(id int)");
    ASSERT_NE(r.error, nullptr);
    EXPECT_EQ(r.error->cursorpos, 8);
    pg_query_free_parse_result(r);
}
