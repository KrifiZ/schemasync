#pragma once

#include "change.h"
#include "db.h"
#include "error.h"
#include "model.h"

#include <string>
#include <vector>

namespace schemasync {

struct Options {
    std::string schema = "public";
    bool dry_run = false;
    bool allow_destructive = false;
    std::string lock_timeout = "3s";
    std::string statement_timeout = "0";
};

struct Outcome {
    std::vector<Change> changes;
    std::vector<std::string> warnings;
};

// Read-only: introspects in a REPEATABLE READ READ ONLY transaction and returns the ordered plan.
Result<Outcome> plan_changes(db::Connection& conn, const Schema& desired, const Options& options);

// Introspects and executes the plan in one transaction, so it is atomic. With dry_run the
// transaction is rolled back after every statement has run.
Result<Outcome> apply_changes(db::Connection& conn, const Schema& desired, const Options& options);

}  // namespace schemasync
