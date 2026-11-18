#pragma once

#include "db.h"
#include "diff.h"
#include "emit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace testutil {

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

inline std::filesystem::path fixtures() { return SCHEMASYNC_FIXTURES; }

inline std::vector<std::string> plan_cases() {
    std::vector<std::string> out;
    for (const auto& entry : std::filesystem::directory_iterator(fixtures() / "plans"))
        if (entry.is_directory()) out.push_back(entry.path().filename().string());
    std::ranges::sort(out);
    return out;
}

inline std::string render_all(const std::vector<schemasync::Change>& changes) {
    std::string out;
    for (const auto& c : changes) out += schemasync::render(c) + "\n\n";
    return out;
}

inline std::string describe_diff(const schemasync::Schema& from, const schemasync::Schema& to) {
    std::vector<std::string> warnings;
    return render_all(schemasync::diff(from, to, warnings));
}

// Creates a fresh database per test from SCHEMASYNC_TEST_DSN (keyword/value form) and drops it afterwards.
class TempDbTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* dsn = std::getenv("SCHEMASYNC_TEST_DSN");
        if (!dsn || !*dsn) GTEST_SKIP() << "SCHEMASYNC_TEST_DSN not set";
        base_dsn_ = dsn;
        auto admin = schemasync::db::Connection::open(base_dsn_);
        ASSERT_TRUE(admin) << admin.error().message;
        admin_.emplace(std::move(*admin));

        std::mt19937_64 rng(std::random_device{}() ^ std::chrono::steady_clock::now().time_since_epoch().count());
        name_ = std::format("schemasync_test_{:x}", rng());
        auto created = admin_->exec("CREATE DATABASE " + name_);
        ASSERT_TRUE(created) << created.error().message;
        auto conn = schemasync::db::Connection::open(dsn_for_db());
        ASSERT_TRUE(conn) << conn.error().message;
        conn_.emplace(std::move(*conn));
    }

    void TearDown() override {
        conn_.reset();
        if (admin_ && !name_.empty()) (void)admin_->exec("DROP DATABASE IF EXISTS " + name_ + " WITH (FORCE)");
    }

    schemasync::db::Connection& conn() { return *conn_; }
    std::string dsn_for_db() const { return base_dsn_ + " dbname=" + name_; }

    void exec_ok(const std::string& sql) {
        auto r = conn_->exec(sql);
        ASSERT_TRUE(r) << schemasync::to_string(r.error());
    }

private:
    std::string base_dsn_;
    std::string name_;
    std::optional<schemasync::db::Connection> admin_;
    std::optional<schemasync::db::Connection> conn_;
};

}  // namespace testutil
