#pragma once

#include <expected>
#include <string>

namespace schemasync {

enum class ErrorKind { db };

struct Error {
    ErrorKind kind;
    std::string message;
    std::string sqlstate{};
};

template <class T>
using Result = std::expected<T, Error>;

}  // namespace schemasync
