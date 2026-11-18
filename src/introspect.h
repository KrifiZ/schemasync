#pragma once

#include "db.h"
#include "error.h"
#include "model.h"

#include <string>
#include <vector>

namespace schemasync {

// Reads the managed schema from pg_catalog. Must run inside a transaction (REPEATABLE READ for a
// consistent snapshot); sets a transaction-local search_path so defaults print unqualified.
Result<Schema> introspect(db::Connection& conn, const std::string& schema, std::vector<std::string>& warnings);

}  // namespace schemasync
