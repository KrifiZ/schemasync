#include "normalize.h"

#include <format>

namespace schemasync {

std::string make_object_name(std::string_view name1, std::string_view name2, std::string_view label) {
    constexpr std::size_t max_identifier = 63;
    const std::size_t overhead = (name2.empty() ? 0 : 1) + (label.empty() ? 0 : label.size() + 1);
    const std::size_t available = max_identifier - overhead;
    std::size_t n1 = name1.size();
    std::size_t n2 = name2.size();
    while (n1 + n2 > available) (n1 > n2 ? n1 : n2)--;

    std::string out(name1.substr(0, n1));
    if (!name2.empty()) out += "_" + std::string(name2.substr(0, n2));
    if (!label.empty()) out += "_" + std::string(label);
    return out;
}

std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names) out += (out.empty() ? "" : "_") + n;
    return out;
}

Result<TypeRef> make_type(const std::string& name, const std::vector<int>& mods, bool array) {
    int typmod = -1;
    if (!mods.empty()) {
        const bool single = mods.size() == 1;
        if ((name == "varchar" || name == "bpchar") && single) {
            typmod = mods[0] + 4;
        } else if (name == "numeric" && mods.size() <= 2) {
            typmod = ((mods[0] << 16) | (mods.size() == 2 ? mods[1] : 0)) + 4;
        } else if ((name == "timestamp" || name == "timestamptz" || name == "time" || name == "timetz" || name == "bit" ||
                    name == "varbit") &&
                   single) {
            typmod = mods[0];
        } else {
            return std::unexpected(Error{ErrorKind::unsupported, std::format("unsupported type modifiers for type {}", name)});
        }
    }
    return TypeRef{array ? "_" + name : name, typmod};
}

std::string render_type(const TypeRef& type) {
    const bool array = type.name.starts_with('_');
    std::string base = array ? type.name.substr(1) : type.name;
    std::string out = base;
    if (type.typmod >= 0) {
        if (base == "varchar" || base == "bpchar") {
            out += std::format("({})", type.typmod - 4);
        } else if (base == "numeric") {
            const int v = type.typmod - 4;
            out += std::format("({},{})", v >> 16, v & 0xffff);
        } else {
            out += std::format("({})", type.typmod);
        }
    }
    return array ? out + "[]" : out;
}

Default serial_default(std::string_view table, std::string_view column) {
    const auto seq = make_object_name(table, column, "seq");
    return Default{std::format("nextval('{}'::regclass)", seq), std::format("nextval(={})", seq)};
}

std::optional<std::string> serial_type(std::string_view table, const Column& column) {
    if (!column.default_value || column.default_value->key != serial_default(table, column.name).key) return std::nullopt;
    if (column.type.name == "int4") return "serial";
    if (column.type.name == "int8") return "bigserial";
    if (column.type.name == "int2") return "smallserial";
    return std::nullopt;
}

Result<void> finalize(Schema& schema) {
    auto fail = [](std::string message) { return std::unexpected(Error{ErrorKind::parse, std::move(message)}); };
    auto missing = [](const Table& t, const std::vector<std::string>& cols) -> std::optional<std::string> {
        for (const auto& c : cols)
            if (!t.column(c)) return c;
        return std::nullopt;
    };

    for (auto& [name, table] : schema.tables) {
        const auto& t = table.name.name;
        if (table.primary_key) {
            if (auto c = missing(table, table.primary_key->columns))
                return fail(std::format("table {}: primary key column \"{}\" does not exist", t, *c));
            for (auto& col : table.columns)
                if (std::ranges::contains(table.primary_key->columns, col.name)) col.not_null = true;
        }
        for (const auto& [n, key] : table.uniques)
            if (auto c = missing(table, key.columns))
                return fail(std::format("table {}: constraint {} uses missing column \"{}\"", t, n, *c));

        for (auto& [n, fk] : table.foreign_keys) {
            if (auto c = missing(table, fk.columns))
                return fail(std::format("table {}: foreign key {} uses missing column \"{}\"", t, n, *c));
            auto ref = schema.tables.find(fk.ref_table);
            if (ref == schema.tables.end())
                return fail(std::format("table {}: foreign key {} references unknown table {}", t, n, fk.ref_table.name));
            if (fk.ref_columns.empty()) {
                if (!ref->second.primary_key)
                    return fail(std::format("table {}: foreign key {} has no columns and {} has no primary key", t, n,
                                            fk.ref_table.name));
                fk.ref_columns = ref->second.primary_key->columns;
            }
            if (auto c = missing(ref->second, fk.ref_columns))
                return fail(std::format("table {}: foreign key {} references missing column {}.{}", t, n,
                                        fk.ref_table.name, *c));
            if (fk.ref_columns.size() != fk.columns.size())
                return fail(std::format("table {}: foreign key {} has mismatched column counts", t, n));
        }
    }

    for (const auto& [name, index] : schema.indexes) {
        auto table = schema.tables.find(index.table);
        if (table == schema.tables.end())
            return fail(std::format("index {} is on unknown table {}", name.name, index.table.name));
        if (auto c = missing(table->second, index.columns))
            return fail(std::format("index {} uses missing column \"{}\"", name.name, *c));
    }
    return {};
}

}  // namespace schemasync
