#include "introspect.h"

#include "parse.h"

#include <nlohmann/json.hpp>

#include <format>

namespace schemasync {

namespace {

constexpr const char* tables_sql = R"(
SELECT c.relname
FROM pg_class c
JOIN pg_namespace n ON n.oid = c.relnamespace
WHERE n.nspname = $1 AND c.relkind = 'r' AND NOT c.relispartition
  AND NOT EXISTS (SELECT 1 FROM pg_depend d
                  WHERE d.classid = 'pg_class'::regclass AND d.objid = c.oid AND d.deptype = 'e')
ORDER BY c.relname)";

constexpr const char* columns_sql = R"(
SELECT c.relname, a.attname, t.typname, a.atttypmod, a.attnotnull,
       pg_get_expr(ad.adbin, ad.adrelid),
       a.attidentity <> '' OR a.attgenerated <> ''
FROM pg_attribute a
JOIN pg_class c ON c.oid = a.attrelid
JOIN pg_namespace n ON n.oid = c.relnamespace
JOIN pg_type t ON t.oid = a.atttypid
LEFT JOIN pg_attrdef ad ON ad.adrelid = a.attrelid AND ad.adnum = a.attnum
WHERE n.nspname = $1 AND c.relkind = 'r' AND a.attnum > 0 AND NOT a.attisdropped
ORDER BY c.relname, a.attnum)";

constexpr const char* constraints_sql = R"(
SELECT c.relname, con.conname, con.contype,
       to_json(ARRAY(SELECT a.attname FROM unnest(con.conkey) WITH ORDINALITY k(num, ord)
                     JOIN pg_attribute a ON a.attrelid = con.conrelid AND a.attnum = k.num
                     ORDER BY k.ord))::text,
       rn.nspname, rc.relname,
       to_json(ARRAY(SELECT a.attname FROM unnest(con.confkey) WITH ORDINALITY k(num, ord)
                     JOIN pg_attribute a ON a.attrelid = con.confrelid AND a.attnum = k.num
                     ORDER BY k.ord))::text,
       con.confdeltype, con.confupdtype, con.convalidated
FROM pg_constraint con
JOIN pg_class c ON c.oid = con.conrelid
JOIN pg_namespace n ON n.oid = c.relnamespace
LEFT JOIN pg_class rc ON rc.oid = con.confrelid
LEFT JOIN pg_namespace rn ON rn.oid = rc.relnamespace
WHERE n.nspname = $1 AND c.relkind = 'r' AND con.contype IN ('p', 'u', 'f', 'c')
ORDER BY c.relname, con.conname)";

// Indexes that back PRIMARY KEY / UNIQUE / EXCLUDE constraints are part of the constraint, not standalone.
constexpr const char* indexes_sql = R"(
SELECT ic.relname, c.relname, i.indisunique, i.indisvalid,
       am.amname <> 'btree' OR i.indexprs IS NOT NULL OR i.indpred IS NOT NULL
         OR i.indnkeyatts <> i.indnatts
         OR EXISTS (SELECT 1 FROM unnest(i.indoption::int2[]) o WHERE o <> 0),
       to_json(ARRAY(SELECT a.attname FROM unnest(i.indkey::int2[]) WITH ORDINALITY k(num, ord)
                     JOIN pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = k.num
                     ORDER BY k.ord))::text
FROM pg_index i
JOIN pg_class ic ON ic.oid = i.indexrelid
JOIN pg_class c ON c.oid = i.indrelid
JOIN pg_namespace n ON n.oid = c.relnamespace
JOIN pg_am am ON am.oid = ic.relam
WHERE n.nspname = $1 AND c.relkind = 'r'
  AND NOT EXISTS (SELECT 1 FROM pg_constraint con
                  WHERE con.conindid = i.indexrelid AND con.conrelid = i.indrelid
                    AND con.contype IN ('p', 'u', 'x'))
ORDER BY ic.relname)";

std::vector<std::string> names(std::string_view json_array) {
    return nlohmann::json::parse(json_array).get<std::vector<std::string>>();
}

}  // namespace

Result<Schema> introspect(db::Connection& conn, const std::string& schema_name, std::vector<std::string>& warnings) {
    if (auto r = conn.exec("SET LOCAL search_path TO " + db::quote_ident(schema_name)); !r) return std::unexpected(r.error());

    const std::string params[] = {schema_name};
    Schema schema;
    auto table = [&](std::string_view name) -> Table* {
        auto it = schema.tables.find({schema_name, std::string(name)});
        return it == schema.tables.end() ? nullptr : &it->second;
    };

    auto tables = conn.exec_params(tables_sql, params);
    if (!tables) return std::unexpected(tables.error());
    for (int i = 0; i < tables->size(); ++i) {
        QualifiedName name{schema_name, std::string(tables->get(i, 0))};
        schema.tables.emplace(name, Table{name});
    }

    auto columns = conn.exec_params(columns_sql, params);
    if (!columns) return std::unexpected(columns.error());
    for (int i = 0; i < columns->size(); ++i) {
        auto* t = table(columns->get(i, 0));
        if (!t) continue;
        const std::string name(columns->get(i, 1));
        if (columns->get(i, 6) == "t")
            return std::unexpected(Error{ErrorKind::unsupported,
                                         std::format("column {}.{}: identity and generated columns are not supported",
                                                     t->name.name, name)});
        Column col{name, TypeRef{std::string(columns->get(i, 2)), std::stoi(std::string(columns->get(i, 3)))},
                   columns->get(i, 4) == "t"};
        if (!columns->is_null(i, 5)) col.default_value = parse_default_expr(std::string(columns->get(i, 5)));
        t->columns.push_back(std::move(col));
    }

    auto constraints = conn.exec_params(constraints_sql, params);
    if (!constraints) return std::unexpected(constraints.error());
    for (int i = 0; i < constraints->size(); ++i) {
        auto* t = table(constraints->get(i, 0));
        if (!t) continue;
        const std::string name(constraints->get(i, 1));
        auto cols = names(constraints->get(i, 3));
        switch (constraints->get(i, 2)[0]) {
            case 'p': t->primary_key = KeyConstraint{name, std::move(cols)}; break;
            case 'u': t->uniques.emplace(name, KeyConstraint{name, std::move(cols)}); break;
            case 'f':
                t->foreign_keys.emplace(
                    name, ForeignKey{name, std::move(cols),
                                     QualifiedName{std::string(constraints->get(i, 4)), std::string(constraints->get(i, 5))},
                                     names(constraints->get(i, 6)), constraints->get(i, 7)[0], constraints->get(i, 8)[0]});
                if (constraints->get(i, 9) != "t")
                    warnings.push_back(std::format("{}: foreign key {} is NOT VALID; it is treated as a regular foreign key",
                                                   t->name.name, name));
                break;
            default:
                warnings.push_back(std::format("{}: CHECK constraint {} is not managed by schemasync and was left untouched",
                                               t->name.name, name));
        }
    }

    auto indexes = conn.exec_params(indexes_sql, params);
    if (!indexes) return std::unexpected(indexes.error());
    for (int i = 0; i < indexes->size(); ++i) {
        auto* t = table(indexes->get(i, 1));
        if (!t) continue;
        const std::string name(indexes->get(i, 0));
        if (indexes->get(i, 4) == "t") {
            warnings.push_back(std::format(
                "index {} is not managed by schemasync (expression, partial, non-btree, INCLUDE or DESC) and was left untouched",
                name));
            continue;
        }
        if (indexes->get(i, 3) != "t")
            warnings.push_back(std::format("index {} is INVALID (failed CREATE INDEX CONCURRENTLY?); drop or rebuild it", name));
        Index index{{schema_name, name}, t->name, names(indexes->get(i, 5)), indexes->get(i, 2) == "t"};
        schema.indexes.emplace(index.name, std::move(index));
    }
    return schema;
}

}  // namespace schemasync
