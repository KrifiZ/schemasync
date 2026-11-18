#include "db.h"

#include <format>
#include <vector>

namespace schemasync::db {

namespace {

std::string trimmed(const char* msg) {
    std::string s = msg ? msg : "";
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

Result<Rows> check(PGconn* conn, PGresult* raw) {
    ResultPtr res(raw);
    if (!res) return std::unexpected(Error{ErrorKind::db, trimmed(PQerrorMessage(conn))});

    const auto status = PQresultStatus(res.get());
    if (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK || status == PGRES_EMPTY_QUERY) return Rows(std::move(res));

    const char* sqlstate = PQresultErrorField(res.get(), PG_DIAG_SQLSTATE);
    return std::unexpected(Error{ErrorKind::db, trimmed(PQresultErrorMessage(res.get())), sqlstate ? sqlstate : ""});
}

}  // namespace

Result<Connection> Connection::open(const std::string& dsn) {
    ConnPtr conn(PQconnectdb(dsn.c_str()));
    if (!conn) return std::unexpected(Error{ErrorKind::db, "out of memory allocating PGconn"});
    if (PQstatus(conn.get()) != CONNECTION_OK) {
        return std::unexpected(Error{ErrorKind::db, trimmed(PQerrorMessage(conn.get()))});
    }
    PQsetNoticeProcessor(conn.get(), [](void*, const char*) {}, nullptr);
    return Connection(std::move(conn));
}

Result<Rows> Connection::exec(const std::string& sql) {
    return check(conn_.get(), PQexec(conn_.get(), sql.c_str()));
}

Result<Rows> Connection::exec_params(const std::string& sql, std::span<const std::string> params) {
    std::vector<const char*> values;
    values.reserve(params.size());
    for (const auto& p : params) values.push_back(p.c_str());
    return check(conn_.get(), PQexecParams(conn_.get(), sql.c_str(), static_cast<int>(values.size()), nullptr,
                                           values.data(), nullptr, nullptr, 0));
}

Result<Transaction> Transaction::begin(Connection& conn, std::string_view options) {
    auto r = conn.exec(options.empty() ? "BEGIN" : std::format("BEGIN {}", options));
    if (!r) return std::unexpected(std::move(r.error()));
    return Transaction(conn);
}

Transaction::~Transaction() {
    if (conn_) PQclear(PQexec(conn_->raw(), "ROLLBACK"));
}

Result<void> Transaction::commit() {
    auto r = conn_->exec("COMMIT");
    conn_ = nullptr;
    if (!r) return std::unexpected(std::move(r.error()));
    return {};
}

std::string quote_ident(std::string_view ident) {
    std::string out = "\"";
    for (char c : ident) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + '"';
}

}  // namespace schemasync::db
