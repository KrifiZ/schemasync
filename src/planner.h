#pragma once

#include "change.h"
#include "error.h"

#include <cstddef>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace schemasync {

// (phase, change kind, object, input position): picks the next node among ready ones, so equal
// inputs always give the same plan.
using SortKey = std::tuple<int, std::size_t, std::string, std::size_t>;

// Kahn's algorithm with a priority queue. Edges are (before, after) index pairs.
// On a cycle, returns the nodes that could not be ordered.
std::expected<std::vector<std::size_t>, std::vector<std::size_t>> topo_sort(
    const std::vector<SortKey>& keys, const std::vector<std::pair<std::size_t, std::size_t>>& edges);

// Builds the dependency graph between changes and returns them in a safe execution order.
Result<std::vector<Change>> plan(std::vector<Change> changes);

int phase(const Change& change);
std::string describe(const Change& change);

}  // namespace schemasync
