#include "sync.h"

#include "diff.h"
#include "emit.h"
#include "introspect.h"
#include "planner.h"

#include <format>

namespace schemasync {

namespace {

constexpr long long advisory_lock_key = 0x7363686d73796e63;  // "schmsync"

Result<Outcome> compute(db::Connection& conn, const Schema& desired, const Options& options) {
    Outcome out;
    auto current = introspect(conn, options.schema, out.warnings);
    if (!current) return std::unexpected(current.error());
    auto changes = plan(diff(*current, desired, out.warnings));
    if (!changes) return std::unexpected(changes.error());
    out.changes = std::move(*changes);
    return out;
}

}  // namespace

Result<Outcome> plan_changes(db::Connection& conn, const Schema& desired, const Options& options) {
    auto tx = db::Transaction::begin(conn, "ISOLATION LEVEL REPEATABLE READ READ ONLY");
    if (!tx) return std::unexpected(tx.error());
    return compute(conn, desired, options);
}

Result<Outcome> apply_changes(db::Connection& conn, const Schema& desired, const Options& options) {
    auto tx = db::Transaction::begin(conn, "ISOLATION LEVEL REPEATABLE READ");
    if (!tx) return std::unexpected(tx.error());

    auto lock = conn.exec(std::format("SELECT pg_try_advisory_xact_lock({})", advisory_lock_key));
    if (!lock) return std::unexpected(lock.error());
    if (lock->get(0, 0) != "t")
        return std::unexpected(Error{ErrorKind::refused, "another schemasync apply is running against this database"});

    const std::string timeouts[] = {options.lock_timeout, options.statement_timeout};
    if (auto r = conn.exec_params("SELECT set_config('lock_timeout', $1, true), set_config('statement_timeout', $2, true)",
                                  timeouts);
        !r)
        return std::unexpected(r.error());

    auto out = compute(conn, desired, options);
    if (!out) return out;

    std::string destructive;
    for (const auto& c : out->changes)
        if (hazards(c) & hazard::destructive) destructive += "\n  " + render(c);
    if (!destructive.empty() && !options.allow_destructive)
        return std::unexpected(
            Error{ErrorKind::refused, "the plan contains destructive changes (rerun with --allow-destructive):" + destructive});

    for (const auto& c : out->changes) {
        const auto sql = render(c);
        if (auto r = conn.exec(sql); !r) {
            auto e = r.error();
            e.message += "\n  while executing: " + sql;
            return std::unexpected(std::move(e));
        }
    }
    if (!options.dry_run)
        if (auto r = tx->commit(); !r) return std::unexpected(r.error());
    return out;
}

}  // namespace schemasync
