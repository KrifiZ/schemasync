#pragma once

#include "error.h"
#include "model.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace schemasync {

// PostgreSQL's makeObjectName: <name1>_<name2>_<label>, truncated to 63 bytes.
std::string make_object_name(std::string_view name1, std::string_view name2, std::string_view label);
std::string join_names(const std::vector<std::string>& names);

Result<TypeRef> make_type(const std::string& name, const std::vector<int>& mods, bool array);
std::string render_type(const TypeRef& type);

Default serial_default(std::string_view table, std::string_view column);
std::optional<std::string> serial_type(std::string_view table, const Column& column);

// Applies implicit rules to a parsed schema: PK columns become NOT NULL, FKs without
// columns reference the target's PK, and every referenced table and column must exist.
Result<void> finalize(Schema& schema);

}  // namespace schemasync
