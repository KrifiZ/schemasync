#include "diff.h"

#include <format>

namespace schemasync {

namespace {

DropConstraint drop_constraint(const QualifiedName& table, const ForeignKey& fk) {
    return {table, fk.name, fk.columns, fk.ref_table, fk.ref_columns};
}

DropConstraint drop_constraint(const QualifiedName& table, const KeyConstraint& key) {
    return {table, key.name, key.columns, std::nullopt, {}};
}

template <class Map, class OnAdd, class OnDrop, class OnChange>
void diff_maps(const Map& current, const Map& desired, OnAdd on_add, OnDrop on_drop, OnChange on_change) {
    for (const auto& [key, want] : desired) {
        auto it = current.find(key);
        if (it == current.end())
            on_add(want);
        else if (!(it->second == want))
            on_change(it->second, want);
    }
    for (const auto& [key, have] : current)
        if (!desired.contains(key)) on_drop(have);
}

void diff_columns(const Table& have, const Table& want, std::vector<Change>& out, std::vector<std::string>& warnings) {
    const auto& t = want.name;
    std::vector<const Column*> added;
    std::vector<const Column*> dropped;

    for (const auto& col : want.columns) {
        const auto* old = have.column(col.name);
        if (!old) {
            out.push_back(AddColumn{t, col});
            added.push_back(&col);
            continue;
        }
        if (old->type != col.type) out.push_back(AlterColumnType{t, col.name, old->type, col.type});
        if (old->default_value != col.default_value) {
            if (col.default_value)
                out.push_back(SetDefault{t, col.name, *col.default_value});
            else
                out.push_back(DropDefault{t, col.name});
        }
        if (old->not_null != col.not_null) {
            if (col.not_null)
                out.push_back(SetNotNull{t, col.name});
            else
                out.push_back(DropNotNull{t, col.name});
        }
    }
    for (const auto& col : have.columns) {
        if (!want.column(col.name)) {
            out.push_back(DropColumn{t, col.name});
            dropped.push_back(&col);
        }
    }

    if (added.size() == 1 && dropped.size() == 1 && added[0]->type == dropped[0]->type)
        warnings.push_back(std::format(
            "{}: column \"{}\" is dropped and \"{}\" is added with the same type. If this is a rename, run "
            "ALTER TABLE ... RENAME COLUMN first; otherwise the data in \"{}\" is lost",
            t.name, dropped[0]->name, added[0]->name, dropped[0]->name));
}

void diff_table(const Table& have, const Table& want, std::vector<Change>& out, std::vector<std::string>& warnings) {
    const auto& t = want.name;
    diff_columns(have, want, out, warnings);

    if (have.primary_key != want.primary_key) {
        if (have.primary_key) out.push_back(drop_constraint(t, *have.primary_key));
        if (want.primary_key) out.push_back(AddPrimaryKey{t, *want.primary_key});
    }
    diff_maps(
        have.uniques, want.uniques, [&](const KeyConstraint& k) { out.push_back(AddUnique{t, k}); },
        [&](const KeyConstraint& k) { out.push_back(drop_constraint(t, k)); },
        [&](const KeyConstraint& old, const KeyConstraint& k) {
            out.push_back(drop_constraint(t, old));
            out.push_back(AddUnique{t, k});
        });
    diff_maps(
        have.foreign_keys, want.foreign_keys, [&](const ForeignKey& fk) { out.push_back(AddForeignKey{t, fk}); },
        [&](const ForeignKey& fk) { out.push_back(drop_constraint(t, fk)); },
        [&](const ForeignKey& old, const ForeignKey& fk) {
            out.push_back(drop_constraint(t, old));
            out.push_back(AddForeignKey{t, fk});
        });
}

}  // namespace

std::vector<Change> diff(const Schema& current, const Schema& desired, std::vector<std::string>& warnings) {
    std::vector<Change> out;
    diff_maps(
        current.tables, desired.tables,
        [&](const Table& t) {
            Table bare = t;
            bare.foreign_keys.clear();
            out.push_back(CreateTable{std::move(bare)});
            for (const auto& [name, fk] : t.foreign_keys) out.push_back(AddForeignKey{t.name, fk});
        },
        [&](const Table& t) {
            // Dropping a table's own FKs explicitly keeps DROP TABLEs free of mutual dependencies.
            for (const auto& [name, fk] : t.foreign_keys) out.push_back(drop_constraint(t.name, fk));
            out.push_back(DropTable{t.name});
        },
        [&](const Table& have, const Table& want) { diff_table(have, want, out, warnings); });

    diff_maps(
        current.indexes, desired.indexes, [&](const Index& i) { out.push_back(CreateIndex{i}); },
        [&](const Index& i) {
            if (desired.tables.contains(i.table)) out.push_back(DropIndex{i});
        },
        [&](const Index& old, const Index& i) {
            out.push_back(DropIndex{old});
            out.push_back(CreateIndex{i});
        });
    return out;
}

}  // namespace schemasync
