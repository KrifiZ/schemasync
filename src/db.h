#pragma once

#include "error.h"

#include <libpq-fe.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace schemasync::db {

struct PGconnDeleter {
    void operator()(PGconn* c) const noexcept { PQfinish(c); }
};
struct PGresultDeleter {
    void operator()(PGresult* r) const noexcept { PQclear(r); }
};
using ConnPtr = std::unique_ptr<PGconn, PGconnDeleter>;
using ResultPtr = std::unique_ptr<PGresult, PGresultDeleter>;

class Rows {
public:
    explicit Rows(ResultPtr res) : res_(std::move(res)) {}
    int size() const noexcept { return PQntuples(res_.get()); }
    std::string_view get(int row, int col) const noexcept { return PQgetvalue(res_.get(), row, col); }
    bool is_null(int row, int col) const noexcept { return PQgetisnull(res_.get(), row, col) != 0; }

private:
    ResultPtr res_;
};

class Connection {
public:
    static Result<Connection> open(const std::string& dsn);

    Result<Rows> exec(const std::string& sql);
    Result<Rows> exec_params(const std::string& sql, std::span<const std::string> params);
    PGconn* raw() const noexcept { return conn_.get(); }

private:
    explicit Connection(ConnPtr conn) : conn_(std::move(conn)) {}
    ConnPtr conn_;
};

class Transaction {
public:
    static Result<Transaction> begin(Connection& conn, std::string_view options = {});

    Transaction(Transaction&& other) noexcept : conn_(std::exchange(other.conn_, nullptr)) {}
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction& operator=(Transaction&&) = delete;
    ~Transaction();

    Result<void> commit();

private:
    explicit Transaction(Connection& conn) : conn_(&conn) {}
    Connection* conn_;
};

std::string quote_ident(std::string_view ident);

}  // namespace schemasync::db
