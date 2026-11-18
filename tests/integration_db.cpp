#include "db.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>

using namespace schemasync;
using namespace schemasync::db;

namespace {

class Db : public ::testing::Test {
protected:
    void SetUp() override {
        const char* dsn = std::getenv("SCHEMASYNC_TEST_DSN");
        if (!dsn || !*dsn) GTEST_SKIP() << "SCHEMASYNC_TEST_DSN not set";
        auto c = Connection::open(dsn);
        ASSERT_TRUE(c) << c.error().message;
        conn.emplace(std::move(*c));
        (void)conn->exec("DROP TABLE IF EXISTS schemasync_tx_test");
    }
    void TearDown() override {
        if (conn) (void)conn->exec("DROP TABLE IF EXISTS schemasync_tx_test");
    }

    bool table_exists() {
        auto r = conn->exec("SELECT to_regclass('schemasync_tx_test') IS NOT NULL");
        return r && r->get(0, 0) == "t";
    }

    std::optional<Connection> conn;
};

}  // namespace

TEST(QuoteIdent, EscapesDoubleQuotes) {
    EXPECT_EQ(quote_ident("users"), "\"users\"");
    EXPECT_EQ(quote_ident("Users"), "\"Users\"");
    EXPECT_EQ(quote_ident(R"(a"b)"), R"("a""b")");
}

TEST(Connect, BadDsnReturnsError) {
    auto c = Connection::open("host=127.0.0.1 port=1 connect_timeout=1");
    ASSERT_FALSE(c);
    EXPECT_EQ(c.error().kind, ErrorKind::db);
    EXPECT_FALSE(c.error().message.empty());
}

TEST_F(Db, SelectReturnsRows) {
    auto r = conn->exec("SELECT 1, NULL");
    ASSERT_TRUE(r) << r.error().message;
    ASSERT_EQ(r->size(), 1);
    EXPECT_EQ(r->get(0, 0), "1");
    EXPECT_TRUE(r->is_null(0, 1));
}

TEST_F(Db, ExecParamsPassesValuesVerbatim) {
    const std::string params[] = {"41", "'; DROP TABLE x; --"};
    auto r = conn->exec_params("SELECT $1::int + 1, $2::text", params);
    ASSERT_TRUE(r) << r.error().message;
    EXPECT_EQ(r->get(0, 0), "42");
    EXPECT_EQ(r->get(0, 1), "'; DROP TABLE x; --");
}

TEST_F(Db, ExecParamsRejectsMultipleStatements) {
    auto r = conn->exec_params("SELECT 1; SELECT 2", {});
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().sqlstate, "42601");
}

TEST_F(Db, MissingTableReportsSqlstate) {
    auto r = conn->exec("SELECT * FROM schemasync_no_such_table");
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().sqlstate, "42P01");
}

TEST_F(Db, TransactionRollsBackWithoutCommit) {
    {
        auto tx = Transaction::begin(*conn);
        ASSERT_TRUE(tx) << tx.error().message;
        ASSERT_TRUE(conn->exec("CREATE TABLE schemasync_tx_test(id int)"));
    }
    EXPECT_FALSE(table_exists());
}

TEST_F(Db, TransactionCommitPersists) {
    auto tx = Transaction::begin(*conn, "ISOLATION LEVEL REPEATABLE READ");
    ASSERT_TRUE(tx) << tx.error().message;
    ASSERT_TRUE(conn->exec("CREATE TABLE schemasync_tx_test(id int)"));
    ASSERT_TRUE(tx->commit());
    EXPECT_TRUE(table_exists());
}
