#include "emit.h"
#include "parse.h"
#include "sync.h"

#include <CLI/CLI.hpp>

#include <print>
#include <string>

using namespace schemasync;

namespace {

void print_outcome(const Outcome& out) {
    for (const auto& w : out.warnings) std::println(stderr, "warning: {}", w);
    for (const auto& c : out.changes) {
        if (auto h = hazards(c)) std::println("-- hazards: {}", hazard_names(h));
        std::println("{}\n", render(c));
    }
}

int fail(const Error& e) {
    std::println(stderr, "error: {}", to_string(e));
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    ParserGuard parser;
    CLI::App app{"schemasync - declarative schema migrations for PostgreSQL"};
    app.fallthrough();

    std::string dsn;
    std::string schema_dir;
    Options options;
    app.add_option("--dsn", dsn, "libpq connection string")->envname("SCHEMASYNC_DSN")->required();
    app.add_option("--schema-dir", schema_dir, "Directory with the desired *.sql files")
        ->check(CLI::ExistingDirectory)
        ->required();
    app.add_option("--schema", options.schema, "Database schema to manage")->capture_default_str();

    auto* plan = app.add_subcommand("plan", "Print the migration plan (exit code 2 if there are changes)");
    auto* apply = app.add_subcommand("apply", "Apply the migration plan in a single transaction");
    apply->add_flag("--dry-run", options.dry_run, "Execute the plan, then ROLLBACK");
    apply->add_flag("--allow-destructive", options.allow_destructive, "Permit DROP TABLE / DROP COLUMN / narrowing type changes");
    apply->add_option("--lock-timeout", options.lock_timeout, "lock_timeout for the migration")->capture_default_str();
    apply->add_option("--statement-timeout", options.statement_timeout, "statement_timeout for the migration (0 = none)")
        ->capture_default_str();
    app.require_subcommand(1);

    CLI11_PARSE(app, argc, argv);

    auto desired = load_schema_files(schema_dir, options.schema);
    if (!desired) return fail(desired.error());
    auto conn = db::Connection::open(dsn);
    if (!conn) return fail(conn.error());

    if (plan->parsed()) {
        auto out = plan_changes(*conn, *desired, options);
        if (!out) return fail(out.error());
        print_outcome(*out);
        if (out->changes.empty()) {
            std::println(stderr, "No changes: schema \"{}\" matches {}.", options.schema, schema_dir);
            return 0;
        }
        std::println(stderr, "{} change(s) planned.", out->changes.size());
        return 2;
    }

    auto out = apply_changes(*conn, *desired, options);
    if (!out) return fail(out.error());
    print_outcome(*out);
    if (out->changes.empty())
        std::println(stderr, "No changes: schema \"{}\" matches {}.", options.schema, schema_dir);
    else if (options.dry_run)
        std::println(stderr, "Dry run: executed {} change(s), then rolled back.", out->changes.size());
    else
        std::println(stderr, "Applied {} change(s).", out->changes.size());
    return 0;
}
