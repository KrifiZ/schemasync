#include "planner.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <queue>

namespace schemasync {

namespace {

using ColumnRef = std::pair<QualifiedName, std::string>;

struct Touches {
    std::vector<QualifiedName> tables;
    std::vector<ColumnRef> columns;
};

void touch(Touches& t, const QualifiedName& table, const std::vector<std::string>& columns) {
    t.tables.push_back(table);
    for (const auto& c : columns) t.columns.emplace_back(table, c);
}

Touches touches(const Change& change) {
    Touches t;
    std::visit(overloaded{
                   [&](const CreateTable& x) { t.tables.push_back(x.table.name); },
                   [&](const DropTable& x) { t.tables.push_back(x.table); },
                   [&](const CreateIndex& x) { touch(t, x.index.table, x.index.columns); },
                   [&](const DropIndex& x) { touch(t, x.index.table, x.index.columns); },
                   [&](const AddPrimaryKey& x) { touch(t, x.table, x.key.columns); },
                   [&](const AddUnique& x) { touch(t, x.table, x.key.columns); },
                   [&](const AddForeignKey& x) {
                       touch(t, x.table, x.fk.columns);
                       touch(t, x.fk.ref_table, x.fk.ref_columns);
                   },
                   [&](const DropConstraint& x) {
                       touch(t, x.table, x.columns);
                       if (x.ref_table) touch(t, *x.ref_table, x.ref_columns);
                   },
                   [&](const AddColumn& x) { touch(t, x.table, {x.column.name}); },
                   [&](const auto& x) { touch(t, x.table, {x.column}); },
               },
               change);
    return t;
}

template <class T>
bool contains(const std::vector<T>& v, const T& x) {
    return std::ranges::find(v, x) != v.end();
}

std::optional<QualifiedName> key_added_on(const Change& c) {
    if (const auto* x = std::get_if<AddPrimaryKey>(&c)) return x->table;
    if (const auto* x = std::get_if<AddUnique>(&c)) return x->table;
    if (const auto* x = std::get_if<CreateIndex>(&c); x && x->index.unique) return x->index.table;
    return std::nullopt;
}

std::optional<QualifiedName> key_dropped_on(const Change& c) {
    if (const auto* x = std::get_if<DropConstraint>(&c); x && !x->ref_table) return x->table;
    if (const auto* x = std::get_if<DropIndex>(&c); x && x->index.unique) return x->index.table;
    return std::nullopt;
}

bool must_precede(const Change& a, const Touches& ta, const Change& b, const Touches& tb) {
    if (const auto* x = std::get_if<CreateTable>(&a); x && !std::holds_alternative<CreateTable>(b) && contains(tb.tables, x->table.name))
        return true;
    if (const auto* x = std::get_if<DropTable>(&b); x && !std::holds_alternative<DropTable>(a) && contains(ta.tables, x->table))
        return true;
    if (const auto* x = std::get_if<AddColumn>(&a); x && contains(tb.columns, ColumnRef{x->table, x->column.name}))
        return true;
    if (const auto* x = std::get_if<DropColumn>(&b); x && contains(ta.columns, ColumnRef{x->table, x->column}))
        return true;
    if (const auto* fk = std::get_if<AddForeignKey>(&b); fk && key_added_on(a) == fk->fk.ref_table) return true;
    if (const auto* fk = std::get_if<DropConstraint>(&a); fk && fk->ref_table && key_dropped_on(b) == fk->ref_table)
        return true;
    if (const auto* d = std::get_if<SetDefault>(&a)) {
        if (const auto* n = std::get_if<SetNotNull>(&b); n && n->table == d->table && n->column == d->column) return true;
    }
    return false;
}

constexpr std::array<const char*, std::variant_size_v<Change>> kind_names{
    "DropConstraint", "DropIndex",   "DropColumn",  "DropTable",     "CreateTable",
    "AddColumn",      "AlterColumnType", "DropDefault", "SetDefault", "DropNotNull",
    "SetNotNull",     "AddPrimaryKey",   "AddUnique",   "CreateIndex", "AddForeignKey"};

std::string qualified(const QualifiedName& n) { return n.schema + "." + n.name; }

}  // namespace

int phase(const Change& change) {
    return std::visit(overloaded{
                          [](const DropConstraint&) { return 1; },
                          [](const DropIndex&) { return 2; },
                          [](const DropColumn&) { return 3; },
                          [](const DropTable&) { return 3; },
                          [](const CreateTable&) { return 4; },
                          [](const AddPrimaryKey&) { return 6; },
                          [](const AddUnique&) { return 6; },
                          [](const CreateIndex&) { return 6; },
                          [](const AddForeignKey&) { return 7; },
                          [](const auto&) { return 5; },
                      },
                      change);
}

std::string describe(const Change& change) {
    const auto object = std::visit(overloaded{
                                       [](const CreateTable& x) { return qualified(x.table.name); },
                                       [](const DropTable& x) { return qualified(x.table); },
                                       [](const CreateIndex& x) { return qualified(x.index.name); },
                                       [](const DropIndex& x) { return qualified(x.index.name); },
                                       [](const AddPrimaryKey& x) { return qualified(x.table) + "." + x.key.name; },
                                       [](const AddUnique& x) { return qualified(x.table) + "." + x.key.name; },
                                       [](const AddForeignKey& x) { return qualified(x.table) + "." + x.fk.name; },
                                       [](const DropConstraint& x) { return qualified(x.table) + "." + x.name; },
                                       [](const AddColumn& x) { return qualified(x.table) + "." + x.column.name; },
                                       [](const auto& x) { return qualified(x.table) + "." + x.column; },
                                   },
                                   change);
    return std::format("{} {}", kind_names[change.index()], object);
}

std::expected<std::vector<std::size_t>, std::vector<std::size_t>> topo_sort(
    const std::vector<SortKey>& keys, const std::vector<std::pair<std::size_t, std::size_t>>& edges) {
    const auto n = keys.size();
    std::vector<std::vector<std::size_t>> next(n);
    std::vector<std::size_t> in_degree(n, 0);
    for (const auto& [from, to] : edges) {
        next[from].push_back(to);
        ++in_degree[to];
    }

    auto later = [&](std::size_t a, std::size_t b) { return keys[a] > keys[b]; };
    std::priority_queue<std::size_t, std::vector<std::size_t>, decltype(later)> ready(later);
    for (std::size_t i = 0; i < n; ++i)
        if (in_degree[i] == 0) ready.push(i);

    std::vector<std::size_t> order;
    order.reserve(n);
    while (!ready.empty()) {
        const auto i = ready.top();
        ready.pop();
        order.push_back(i);
        for (auto j : next[i])
            if (--in_degree[j] == 0) ready.push(j);
    }

    if (order.size() < n) {
        std::vector<std::size_t> stuck;
        for (std::size_t i = 0; i < n; ++i)
            if (in_degree[i] > 0) stuck.push_back(i);
        return std::unexpected(std::move(stuck));
    }
    return order;
}

Result<std::vector<Change>> plan(std::vector<Change> changes) {
    std::vector<SortKey> keys;
    std::vector<Touches> touched;
    for (std::size_t i = 0; i < changes.size(); ++i) {
        const auto& c = changes[i];
        // New columns sort by table only, so they keep the declared column order from diff().
        const auto* add = std::get_if<AddColumn>(&c);
        keys.emplace_back(phase(c), c.index(), add ? qualified(add->table) : describe(c), i);
        touched.push_back(touches(c));
    }

    // ponytail: O(n^2) pairwise rule check; fine for hundreds of changes, index by table if plans get huge.
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (std::size_t i = 0; i < changes.size(); ++i)
        for (std::size_t j = 0; j < changes.size(); ++j)
            if (i != j && must_precede(changes[i], touched[i], changes[j], touched[j])) edges.emplace_back(i, j);

    auto order = topo_sort(keys, edges);
    if (!order) {
        std::string nodes;
        for (auto i : order.error()) nodes += "\n  " + describe(changes[i]);
        return std::unexpected(Error{ErrorKind::cycle, "dependency cycle between changes:" + nodes});
    }
    std::vector<Change> out;
    out.reserve(changes.size());
    for (auto i : *order) out.push_back(std::move(changes[i]));
    return out;
}

}  // namespace schemasync
