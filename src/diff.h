#pragma once

#include "change.h"
#include "model.h"

#include <string>
#include <vector>

namespace schemasync {

// Unordered list of changes that turn current into desired; plan() orders them.
std::vector<Change> diff(const Schema& current, const Schema& desired, std::vector<std::string>& warnings);

}  // namespace schemasync
