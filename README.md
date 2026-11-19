# schemasync

[![CI](https://github.com/KrifiZ/schemasync/actions/workflows/ci.yml/badge.svg)](https://github.com/KrifiZ/schemasync/actions/workflows/ci.yml)

Declarative schema migrations for PostgreSQL, written in C++23. You describe the schema you want in
plain `CREATE TABLE` / `CREATE INDEX` files; schemasync compares them with a live database and
generates the `ALTER` statements that get it there, in a dependency-safe order.

```console
$ schemasync --dsn "dbname=blog" --schema-dir examples/blog/schema plan
-- hazards: DESTRUCTIVE, ACCESS_EXCLUSIVE_LOCK
ALTER TABLE "public"."users" DROP COLUMN "display_name";

-- hazards: ACCESS_EXCLUSIVE_LOCK
ALTER TABLE "public"."users" ADD COLUMN "bio" varchar(280);

2 change(s) planned.                                   # exit code 2

$ schemasync --dsn "dbname=blog" --schema-dir examples/blog/schema apply
error: the plan contains destructive changes (rerun with --allow-destructive):
  ALTER TABLE "public"."users" DROP COLUMN "display_name";

$ schemasync --dsn "dbname=blog" --schema-dir examples/blog/schema apply --allow-destructive
Applied 2 change(s).

$ schemasync --dsn "dbname=blog" --schema-dir examples/blog/schema plan
No changes: schema "public" matches examples/blog/schema.   # exit code 0
```

This is an educational project. For production use, look at [Atlas](https://atlasgo.io),
[pg-schema-diff](https://github.com/stripe/pg-schema-diff) or [sqldef](https://github.com/sqldef/sqldef).

## Commands

| Command | What it does | Exit code |
|---|---|---|
| `plan` | Read-only. Prints the ordered SQL plan with hazards and warnings. | 0 = in sync, 2 = changes, 1 = error |
| `apply --dry-run` | Executes the whole plan inside a transaction, then rolls back. Catches what a static plan cannot (existing NULLs, FK violations, bad casts). Takes locks, so don't run it on a busy production database. | 0 / 1 |
| `apply` | Executes the plan in one transaction and commits. | 0 / 1 |

Options: `--dsn` (or `SCHEMASYNC_DSN`), `--schema-dir`, `--schema` (default `public`), and for
`apply`: `--allow-destructive`, `--lock-timeout` (default `3s`), `--statement-timeout` (default `0`, off).

`plan`'s exit code makes drift detection in CI a one-liner: anything other than `0` fails the job.

## How it works

```
 schema/*.sql ──► libpg_query (PostgreSQL's own parser) ──► AST → model ──┐
                                                                         ├──► diff ──► std::vector<Change>
 live database ──► libpq: pg_catalog queries (REPEATABLE READ) ──► model ─┘                │
                                                                                            ▼
                     dependency graph + Kahn's topological sort ──► hazard analysis ──► SQL emitter
                                                                                            │
                                         plan (stdout) │ apply --dry-run (ROLLBACK) │ apply (COMMIT)
```

1. **Parse.** Each `*.sql` file in `--schema-dir` is read in alphabetical order and parsed by
   [libpg_query](https://github.com/pganalyze/libpg_query), which compiles the PostgreSQL 18 parser
   into a library. Errors point at the exact spot: `users.sql:12:5: syntax error at or near "TABL"`.
   Statements schemasync does not manage (views, functions, triggers…) are errors, never silently
   skipped, because skipping them would make later plans drop things unexpectedly.
2. **Introspect.** Tables, columns, constraints and indexes are read from `pg_catalog` in a single
   `REPEATABLE READ` snapshot. Indexes that back a PRIMARY KEY or UNIQUE constraint belong to the
   constraint; extension-owned tables are skipped; unmanaged objects (CHECK constraints, expression or
   partial indexes) produce warnings and are left alone.
3. **Normalize.** Both sides end up in the same model, so hand-written SQL compares equal to what the
   catalog reports. See [Normalization](#normalization).
4. **Diff.** Per object class: only in the files → create, only in the database → drop (destructive),
   in both → column-level changes. A changed index, primary key, unique constraint or foreign key is
   dropped and recreated.
5. **Plan.** Changes form a graph (`CREATE TABLE` before an index on it, `ADD COLUMN` before a foreign
   key using it, FK drop before dropping the referenced key or table, `SET DEFAULT` before
   `SET NOT NULL`…). Kahn's algorithm orders it; a priority queue keyed on
   *(phase, change kind, object)* makes the plan deterministic. Foreign keys are never part of
   `CREATE TABLE`, so mutual and self-referencing FKs cannot form a cycle. If a cycle ever appears,
   planning fails and lists the changes involved instead of guessing.
6. **Apply.** One transaction: `pg_try_advisory_xact_lock` guards against concurrent runs,
   `lock_timeout` and `statement_timeout` are set, the schema is introspected again inside the
   transaction, and every statement runs. Any failure rolls the whole plan back.

## Supported

| Object | Supported | Not yet |
|---|---|---|
| Tables | create, drop | partitioning, inheritance, unlogged/temp, storage options |
| Columns | built-in and user types by name, arrays, `varchar(n)`, `numeric(p,s)`, `timestamp(p)`, `serial`/`bigserial`/`smallserial`, `NOT NULL`, `DEFAULT` (constants, casts, function calls, `CURRENT_TIMESTAMP`…) | identity and generated columns, collations, complex DEFAULT expressions |
| Constraints | `PRIMARY KEY`, `UNIQUE`, `FOREIGN KEY` with `ON DELETE`/`ON UPDATE`, inline, table-level or via `ALTER TABLE … ADD CONSTRAINT` | CHECK, EXCLUDE, deferrable, `NOT VALID` |
| Indexes | btree column indexes, `UNIQUE` | expression, partial, DESC, INCLUDE, non-btree, `CONCURRENTLY` builds |
| Scope | one schema per run (`--schema`) | cross-schema references, renames, views, functions, enums, sequences, grants, data migrations |

Renames cannot be inferred from two states; a rename looks like DROP + ADD. When one column
disappears and one of the same type appears, schemasync prints a warning suggesting
`ALTER TABLE … RENAME COLUMN` instead of guessing.

## Normalization

| Problem | Example | Strategy |
|---|---|---|
| Type aliases | `int`, `integer`, `int4`; `character varying(10)` | libpg_query already maps aliases to `pg_catalog` names; compare `pg_type.typname` + `atttypmod` (e.g. `varchar(n)` → n + 4, `numeric(p,s)` → `(p << 16 | s) + 4`). |
| `serial` | pseudo-type in DDL, `int4` + `nextval('t_id_seq'::regclass)` in the catalog | expanded at parse time; rendered back as `serial` when the default matches. |
| DEFAULT expressions | `now()` vs `CURRENT_TIMESTAMP`, `'x'` vs `'x'::character varying`, `-1` vs `'-1'::integer` | both sides are parsed with libpg_query and reduced to a canonical key (casts of constants dropped, `now()` ≡ `CURRENT_TIMESTAMP`). |
| Constraint and index names | `PRIMARY KEY (id)` → `users_pkey` | PostgreSQL's `makeObjectName` rules, including truncation to 63 bytes. |
| Identifier case | `Users` → `users`, `"Users"` stays | handled by the parser; all emitted identifiers are quoted. |

## Safety

Every change gets hazard flags, printed above its statement:

| Hazard | Raised by |
|---|---|
| `DESTRUCTIVE` | `DROP TABLE`, `DROP COLUMN`, narrowing type changes (`int8` → `int4`, `varchar(100)` → `varchar(50)`, `text` → `int4`). `apply` refuses these without `--allow-destructive`. |
| `TABLE_REWRITE` | type changes that are not binary-coercible, `ADD COLUMN` with a volatile default |
| `ACCESS_EXCLUSIVE_LOCK` | most `ALTER TABLE` forms (blocks reads and writes) |
| `INDEX_BUILD` | `CREATE INDEX`, new primary keys and unique constraints (blocks writes) |
| `VALIDATION_SCAN` | new foreign keys, `SET NOT NULL` |

`apply` sets `lock_timeout` (default `3s`) so a migration waiting behind a long transaction fails fast
instead of queueing all traffic behind its `ACCESS EXCLUSIVE` request. Because PostgreSQL DDL is
transactional, the plan is atomic: it either fully applies or leaves the database untouched.

## Design decisions

- **libpg_query instead of a shadow database.** Atlas, pg-schema-diff and Prisma normalize by applying the
  desired schema to a temporary database and introspecting both. That is robust but needs a second
  database for every `plan`. Parsing works offline and gives precise error positions, at the cost of
  writing the normalization by hand; DEFAULT expressions are the hardest part. A `--shadow-dsn` mode would
  reuse the same `introspect()` function.
- **`std::variant` for changes.** The set of change kinds is closed while the operations on it keep growing
  (render, hazards, dependency rules, description). `std::visit` with an `overloaded` visitor makes the
  compiler check every visitor for completeness; changes are values with defaulted `operator==`, which keeps
  tests simple.
- **`std::expected` instead of exceptions.** Parse errors, refused plans and lock timeouts are expected
  outcomes, not exceptional ones. `Result<T> = std::expected<T, Error>` keeps the failure path in the
  signature and matches the C APIs underneath, which report errors as values.
- **RAII over the C libraries.** `PGconn` and `PGresult` live in `std::unique_ptr` with empty deleter types
  (no size overhead), `Transaction` rolls back in its destructor unless committed, and the libpg_query parse
  result and its per-thread memory context are released by guards.
- **Phases + Kahn.** The dependency rules alone define correctness; phases (drop constraints → drop indexes →
  drop columns/tables → create tables → alter columns → keys and indexes → foreign keys) only break ties, so
  plans read top to bottom the way a human would write them.
- **JSON parse tree.** libpg_query's JSON output needs no protobuf toolchain and is easy to debug. All
  AST access lives in `src/parse.cpp`.

## Building

Requirements: CMake 3.25+, a C++23 compiler (GCC 14+, Clang 19+, MSVC 19.40+), Ninja and libpq.
CLI11, nlohmann/json, GoogleTest and libpg_query 18.1.0 are fetched and built by CMake.

```sh
# Linux / WSL
sudo apt install libpq-dev ninja-build
cmake --preset linux-debug
cmake --build ~/build/schemasync/linux-debug

# Windows (Developer PowerShell; libpq comes from vcpkg, VCPKG_ROOT must be set)
cmake --preset x64-debug
cmake --build out/build/x64-debug
```

## Testing

```sh
docker compose up -d     # PostgreSQL 18 on localhost:5433
export SCHEMASYNC_TEST_DSN="host=localhost port=5433 user=postgres password=postgres dbname=postgres"
ctest --test-dir ~/build/schemasync/linux-debug --output-on-failure
```

Without `SCHEMASYNC_TEST_DSN` the database tests are skipped. Each database test runs in its own
temporary database.

- **Unit tests** (no database): parser → model, type normalization (parameterized), diff, topological sort and
  cycle detection, SQL rendering and hazards.
- **Golden plans:** every directory in `tests/fixtures/plans/` holds `from.sql`, `to.sql` and the expected
  plan. Set `SCHEMASYNC_UPDATE_GOLDEN=1` to regenerate the expected files after an intended change.
- **Parse = introspect:** each fixture is executed in PostgreSQL and the introspected model must equal the
  parsed one. This verifies normalization against the real catalog.
- **Convergence:** apply `from.sql`, run `apply` towards `to.sql`, then introspect: the model must equal the
  desired one and a second `plan` must be empty.
- **Behavior:** dry-run rollback, destructive-change refusal, whole-plan rollback on failure, and
  `lock_timeout` (another session holds the lock, the migration fails with SQLSTATE `55P03`).

CI runs GCC and Clang against PostgreSQL 16, 17 and 18, plus an ASan + UBSan job and an MSVC build.

## Layout

```
src/
  model.h        schema model shared by both sides
  parse.cpp      libpg_query AST → model, DEFAULT canonicalization
  introspect.cpp pg_catalog → model
  normalize.cpp  PostgreSQL naming rules, typmods, serial, implicit constraints
  diff.cpp       model × model → std::vector<Change>
  planner.cpp    dependency graph and topological sort
  emit.cpp       Change → SQL, hazards
  sync.cpp       plan / apply orchestration and transactions
  db.cpp         RAII wrappers over libpq
  main.cpp       CLI11 front end
tests/           GoogleTest suites and SQL fixtures
examples/blog/   demo schema
```
