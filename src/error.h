#pragma once

#include <expected>
#include <format>
#include <string>

namespace schemasync {

enum class ErrorKind { db, io, parse, unsupported, cycle, refused };

struct Error {
    ErrorKind kind;
    std::string message;
    std::string sqlstate{};
    std::string file{};
    int line = 0;
    int col = 0;
    bool operator==(const Error&) const = default;
};

template <class T>
using Result = std::expected<T, Error>;

inline std::string to_string(const Error& e) {
    std::string out;
    if (!e.file.empty()) out = e.line ? std::format("{}:{}:{}: ", e.file, e.line, e.col) : e.file + ": ";
    out += e.message;
    if (!e.sqlstate.empty()) out += std::format(" [SQLSTATE {}]", e.sqlstate);
    return out;
}

}  // namespace schemasync
