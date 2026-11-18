#pragma once

#include "model.h"

#include <variant>

namespace schemasync {

struct DropConstraint {
    QualifiedName table;
    std::string name;
    std::vector<std::string> columns;
    std::optional<QualifiedName> ref_table;
    std::vector<std::string> ref_columns;
    bool operator==(const DropConstraint&) const = default;
};
struct DropIndex {
    Index index;
    bool operator==(const DropIndex&) const = default;
};
struct DropColumn {
    QualifiedName table;
    std::string column;
    bool operator==(const DropColumn&) const = default;
};
struct DropTable {
    QualifiedName table;
    bool operator==(const DropTable&) const = default;
};
// The table never carries foreign keys; those are separate AddForeignKey changes.
struct CreateTable {
    Table table;
    bool operator==(const CreateTable&) const = default;
};
struct AddColumn {
    QualifiedName table;
    Column column;
    bool operator==(const AddColumn&) const = default;
};
struct AlterColumnType {
    QualifiedName table;
    std::string column;
    TypeRef from;
    TypeRef to;
    bool operator==(const AlterColumnType&) const = default;
};
struct DropDefault {
    QualifiedName table;
    std::string column;
    bool operator==(const DropDefault&) const = default;
};
struct SetDefault {
    QualifiedName table;
    std::string column;
    Default value;
    bool operator==(const SetDefault&) const = default;
};
struct DropNotNull {
    QualifiedName table;
    std::string column;
    bool operator==(const DropNotNull&) const = default;
};
struct SetNotNull {
    QualifiedName table;
    std::string column;
    bool operator==(const SetNotNull&) const = default;
};
struct AddPrimaryKey {
    QualifiedName table;
    KeyConstraint key;
    bool operator==(const AddPrimaryKey&) const = default;
};
struct AddUnique {
    QualifiedName table;
    KeyConstraint key;
    bool operator==(const AddUnique&) const = default;
};
struct CreateIndex {
    Index index;
    bool operator==(const CreateIndex&) const = default;
};
struct AddForeignKey {
    QualifiedName table;
    ForeignKey fk;
    bool operator==(const AddForeignKey&) const = default;
};

// Alternatives are listed in execution order; the planner uses the index as a tie-breaker.
using Change = std::variant<DropConstraint, DropIndex, DropColumn, DropTable, CreateTable, AddColumn, AlterColumnType,
                            DropDefault, SetDefault, DropNotNull, SetNotNull, AddPrimaryKey, AddUnique, CreateIndex,
                            AddForeignKey>;

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

}  // namespace schemasync
