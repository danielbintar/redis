#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// RESP (Redis Serialization Protocol) parser and builder
namespace resp {

// Parse one complete command from buffer.
// Returns the command tokens and erases consumed bytes from buf.
// Returns nullopt if buffer doesn't hold a complete command yet.
std::optional<std::vector<std::string>> parse(std::string& buf);

// Builders — produce wire-format RESP strings
std::string ok();
std::string simpleString(const std::string& s);
std::string error(const std::string& msg);
std::string integer(int64_t n);
std::string bulkString(const std::string& s);
std::string nullBulk();
std::string array(const std::vector<std::string>& items);

}  // namespace resp
