#pragma once

#include "error.h"
#include "model.h"

#include <filesystem>
#include <optional>
#include <string>

namespace schemasync {

Result<Schema> parse_string(const std::string& sql, const std::string& schema = "public",
                            const std::string& file = "<input>");

// Reads every *.sql file in dir in alphabetical order.
Result<Schema> load_schema_files(const std::filesystem::path& dir, const std::string& schema);

// Canonicalizes a default expression as printed by pg_get_expr. Returns nullopt for NULL.
std::optional<Default> parse_default_expr(const std::string& expr);

// Releases libpg_query's per-thread memory context.
struct ParserGuard {
    ParserGuard() = default;
    ParserGuard(const ParserGuard&) = delete;
    ParserGuard& operator=(const ParserGuard&) = delete;
    ~ParserGuard();
};

}  // namespace schemasync
