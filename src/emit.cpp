#include "emit.h"

#include "db.h"
#include "normalize.h"

#include <array>
#include <format>
#include <map>
#include <utility>

namespace schemasync {

namespace {

using db::quote_ident;

std::string qualified(const QualifiedName& n) { return quote_ident(n.schema) + "." + quote_ident(n.name); }

std::string column_list(const std::vector<std::string>& columns) {
    std::string out;
    for (const auto& c : columns) out += (out.empty() ? "" : ", ") + quote_ident(c);
    return "(" + out + ")";
}

std::string column_sql(const QualifiedName& table, const Column& c) {
    const auto serial = serial_type(table.name, c);
    std::string out = quote_ident(c.name) + " " + (serial ? *serial : render_type(c.type));
    if (c.not_null) out += " NOT NULL";
    if (c.default_value && !serial) out += " DEFAULT " + c.default_value->sql;
    return out;
}

std::string fk_action(char action, const char* event) {
    switch (action) {
        case 'r': return std::format(" ON {} RESTRICT", event);
        case 'c': return std::format(" ON {} CASCADE", event);
        case 'n': return std::format(" ON {} SET NULL", event);
        case 'd': return std::format(" ON {} SET DEFAULT", event);
        default: return "";
    }
}

std::string alter(const QualifiedName& table) { return "ALTER TABLE " + qualified(table) + " "; }

std::string alter_column(const QualifiedName& table, const std::string& column) {
    return alter(table) + "ALTER COLUMN " + quote_ident(column) + " ";
}

int numeric_precision(int typmod) { return (typmod - 4) >> 16; }
int numeric_scale(int typmod) { return (typmod - 4) & 0xffff; }

bool rewrites(const TypeRef& from, const TypeRef& to) {
    if (!widens(from, to)) return true;
    if (from.name == "varchar" || from.name == "text") return false;
    if (from.name == "numeric" && to.name == "numeric")
        return !(to.typmod == -1 || (from.typmod != -1 && numeric_scale(from.typmod) == numeric_scale(to.typmod)));
    return true;
}

}  // namespace

std::string render(const Change& change) {
    return std::visit(
        overloaded{
            [](const CreateTable& x) {
                std::vector<std::string> items;
                for (const auto& c : x.table.columns) items.push_back(column_sql(x.table.name, c));
                if (const auto& pk = x.table.primary_key)
                    items.push_back("CONSTRAINT " + quote_ident(pk->name) + " PRIMARY KEY " + column_list(pk->columns));
                for (const auto& [name, key] : x.table.uniques)
                    items.push_back("CONSTRAINT " + quote_ident(name) + " UNIQUE " + column_list(key.columns));
                std::string out = "CREATE TABLE " + qualified(x.table.name) + " (";
                for (std::size_t i = 0; i < items.size(); ++i) out += (i ? ",\n    " : "\n    ") + items[i];
                return out + "\n);";
            },
            [](const DropTable& x) { return "DROP TABLE " + qualified(x.table) + ";"; },
            [](const AddColumn& x) { return alter(x.table) + "ADD COLUMN " + column_sql(x.table, x.column) + ";"; },
            [](const DropColumn& x) { return alter(x.table) + "DROP COLUMN " + quote_ident(x.column) + ";"; },
            [](const AlterColumnType& x) {
                const auto type = render_type(x.to);
                return alter_column(x.table, x.column) + "TYPE " + type + " USING " + quote_ident(x.column) + "::" + type + ";";
            },
            [](const SetNotNull& x) { return alter_column(x.table, x.column) + "SET NOT NULL;"; },
            [](const DropNotNull& x) { return alter_column(x.table, x.column) + "DROP NOT NULL;"; },
            [](const SetDefault& x) { return alter_column(x.table, x.column) + "SET DEFAULT " + x.value.sql + ";"; },
            [](const DropDefault& x) { return alter_column(x.table, x.column) + "DROP DEFAULT;"; },
            [](const CreateIndex& x) {
                return std::string(x.index.unique ? "CREATE UNIQUE INDEX " : "CREATE INDEX ") + quote_ident(x.index.name.name) +
                       " ON " + qualified(x.index.table) + " " + column_list(x.index.columns) + ";";
            },
            [](const DropIndex& x) { return "DROP INDEX " + qualified(x.index.name) + ";"; },
            [](const AddPrimaryKey& x) {
                return alter(x.table) + "ADD CONSTRAINT " + quote_ident(x.key.name) + " PRIMARY KEY " + column_list(x.key.columns) + ";";
            },
            [](const AddUnique& x) {
                return alter(x.table) + "ADD CONSTRAINT " + quote_ident(x.key.name) + " UNIQUE " + column_list(x.key.columns) + ";";
            },
            [](const AddForeignKey& x) {
                return alter(x.table) + "ADD CONSTRAINT " + quote_ident(x.fk.name) + " FOREIGN KEY " + column_list(x.fk.columns) +
                       " REFERENCES " + qualified(x.fk.ref_table) + " " + column_list(x.fk.ref_columns) +
                       fk_action(x.fk.on_delete, "DELETE") + fk_action(x.fk.on_update, "UPDATE") + ";";
            },
            [](const DropConstraint& x) { return alter(x.table) + "DROP CONSTRAINT " + quote_ident(x.name) + ";"; },
        },
        change);
}

bool widens(const TypeRef& from, const TypeRef& to) {
    static const std::map<std::string, int> integers{{"int2", 1}, {"int4", 2}, {"int8", 3}};
    static const std::map<std::string, int> floats{{"float4", 1}, {"float8", 2}};
    auto ranked = [&](const std::map<std::string, int>& ranks) {
        auto a = ranks.find(from.name);
        auto b = ranks.find(to.name);
        return a != ranks.end() && b != ranks.end() && b->second >= a->second;
    };
    if (ranked(integers) || ranked(floats)) return true;
    if (from.name == "varchar" || from.name == "text") {
        if (to.name == "text") return true;
        if (to.name != "varchar") return false;
        if (to.typmod == -1) return true;
        return from.name == "varchar" && from.typmod != -1 && to.typmod >= from.typmod;
    }
    if (from.name == "numeric" && to.name == "numeric") {
        if (to.typmod == -1) return true;
        if (from.typmod == -1) return false;
        const int fp = numeric_precision(from.typmod), fs = numeric_scale(from.typmod);
        const int tp = numeric_precision(to.typmod), ts = numeric_scale(to.typmod);
        return ts >= fs && tp - ts >= fp - fs;
    }
    return false;
}

unsigned hazards(const Change& change) {
    using namespace hazard;
    return std::visit(overloaded{
                          [](const CreateTable&) { return 0u; },
                          [](const DropTable&) { return destructive | access_exclusive; },
                          [](const DropColumn&) { return destructive | access_exclusive; },
                          [](const AlterColumnType& x) {
                              return access_exclusive | (rewrites(x.from, x.to) ? table_rewrite : 0u) |
                                     (widens(x.from, x.to) ? 0u : destructive);
                          },
                          [](const AddColumn& x) {
                              const auto& d = x.column.default_value;
                              // ponytail: any function call in a default counts as volatile; now() maps to
                              // CURRENT_TIMESTAMP (no parentheses) so it is correctly treated as rewrite-free.
                              const bool volatile_default = d && d->key.find('(') != std::string::npos;
                              return access_exclusive | (volatile_default ? table_rewrite : 0u) |
                                     (x.column.not_null && !d ? validation_scan : 0u);
                          },
                          [](const SetNotNull&) { return validation_scan | access_exclusive; },
                          [](const CreateIndex&) { return index_build; },
                          [](const AddPrimaryKey&) { return index_build | access_exclusive; },
                          [](const AddUnique&) { return index_build | access_exclusive; },
                          [](const AddForeignKey&) { return validation_scan; },
                          [](const auto&) { return access_exclusive; },
                      },
                      change);
}

std::string hazard_names(unsigned flags) {
    static constexpr std::array<std::pair<unsigned, const char*>, 5> names{{
        {hazard::destructive, "DESTRUCTIVE"},
        {hazard::table_rewrite, "TABLE_REWRITE"},
        {hazard::access_exclusive, "ACCESS_EXCLUSIVE_LOCK"},
        {hazard::index_build, "INDEX_BUILD"},
        {hazard::validation_scan, "VALIDATION_SCAN"},
    }};
    std::string out;
    for (const auto& [flag, name] : names)
        if (flags & flag) out += (out.empty() ? "" : ", ") + std::string(name);
    return out;
}

}  // namespace schemasync
