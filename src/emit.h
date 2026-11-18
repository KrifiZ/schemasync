#pragma once

#include "change.h"

#include <string>

namespace schemasync {

namespace hazard {
constexpr unsigned destructive = 1u << 0;       // data loss: DROP TABLE / DROP COLUMN / narrowing type change
constexpr unsigned table_rewrite = 1u << 1;     // rewrites the table and its indexes
constexpr unsigned access_exclusive = 1u << 2;  // blocks reads and writes while it runs
constexpr unsigned index_build = 1u << 3;       // blocks writes while the index builds
constexpr unsigned validation_scan = 1u << 4;   // scans existing rows to validate them
}  // namespace hazard

std::string render(const Change& change);
unsigned hazards(const Change& change);
std::string hazard_names(unsigned flags);
bool widens(const TypeRef& from, const TypeRef& to);

}  // namespace schemasync
