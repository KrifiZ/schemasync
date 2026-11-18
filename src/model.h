#pragma once

#include <algorithm>
#include <compare>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace schemasync {

struct QualifiedName {
    std::string schema;
    std::string name;
    auto operator<=>(const QualifiedName&) const = default;
};

// name is pg_type.typname (int4, varchar, _text for text[]); typmod uses the pg_attribute.atttypmod encoding.
struct TypeRef {
    std::string name;
    int typmod = -1;
    bool operator==(const TypeRef&) const = default;
};

// sql is emitted verbatim; key is the canonical form, so `now()` equals `CURRENT_TIMESTAMP`.
struct Default {
    std::string sql;
    std::string key;
    bool operator==(const Default& o) const { return key == o.key; }
};

struct Column {
    std::string name;
    TypeRef type;
    bool not_null = false;
    std::optional<Default> default_value;
    bool operator==(const Column&) const = default;
};

struct KeyConstraint {
    std::string name;
    std::vector<std::string> columns;
    bool operator==(const KeyConstraint&) const = default;
};

// Actions use pg_constraint.confdeltype codes: a = no action, r = restrict, c = cascade, n = set null, d = set default.
struct ForeignKey {
    std::string name;
    std::vector<std::string> columns;
    QualifiedName ref_table;
    std::vector<std::string> ref_columns;
    char on_delete = 'a';
    char on_update = 'a';
    bool operator==(const ForeignKey&) const = default;
};

struct Table {
    QualifiedName name;
    std::vector<Column> columns;
    std::optional<KeyConstraint> primary_key;
    std::map<std::string, KeyConstraint> uniques;
    std::map<std::string, ForeignKey> foreign_keys;

    const Column* column(std::string_view n) const {
        auto it = std::ranges::find(columns, n, &Column::name);
        return it == columns.end() ? nullptr : &*it;
    }
    bool operator==(const Table&) const = default;
};

struct Index {
    QualifiedName name;
    QualifiedName table;
    std::vector<std::string> columns;
    bool unique = false;
    bool operator==(const Index&) const = default;
};

struct Schema {
    std::map<QualifiedName, Table> tables;
    std::map<QualifiedName, Index> indexes;
    bool operator==(const Schema&) const = default;
};

}  // namespace schemasync
