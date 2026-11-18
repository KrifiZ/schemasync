#include <CLI/CLI.hpp>

#include <print>
#include <string>

int main(int argc, char** argv) {
    CLI::App app{"schemasync - declarative schema migrations for PostgreSQL"};
    std::string dsn;
    std::string schema_dir;
    std::string schema = "public";
    bool dry_run = false;
    bool allow_destructive = false;

    app.add_option("--dsn", dsn, "libpq connection string")->envname("SCHEMASYNC_DSN")->required();
    app.add_option("--schema-dir", schema_dir, "Directory with desired *.sql files")
        ->check(CLI::ExistingDirectory)
        ->required();
    app.add_option("--schema", schema, "Database schema to manage")->capture_default_str();

    auto* plan = app.add_subcommand("plan", "Show the migration plan");
    auto* apply = app.add_subcommand("apply", "Apply the migration plan");
    apply->add_flag("--dry-run", dry_run, "Run inside a transaction and ROLLBACK");
    apply->add_flag("--allow-destructive", allow_destructive, "Permit DROP / narrowing changes");
    app.require_subcommand(1);

    CLI11_PARSE(app, argc, argv);

    std::println(stderr, "{}: not implemented yet", plan->parsed() ? "plan" : "apply");
    return 1;
}
