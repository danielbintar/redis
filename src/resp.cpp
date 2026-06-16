#include "resp.h"

#include <sstream>
#include <stdexcept>

namespace resp {

// ── Parser ────────────────────────────────────────────────────────────────────

static std::optional<std::vector<std::string>> parseInline(std::string& buf) {
    auto pos = buf.find('\n');
    if (pos == std::string::npos) return std::nullopt;
    std::string line = buf.substr(0, pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    buf.erase(0, pos + 1);

    std::vector<std::string> tokens;
    std::istringstream       iss(line);
    std::string              tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

static std::optional<std::vector<std::string>> parseArray(std::string& buf) {
    auto crlf = buf.find("\r\n");
    if (crlf == std::string::npos) return std::nullopt;

    int count;
    try {
        count = std::stoi(buf.substr(1, crlf - 1));
    } catch (...) {
        buf.clear();
        return std::vector<std::string>{};
    }

    if (count <= 0) {
        buf.erase(0, crlf + 2);
        return std::vector<std::string>{};
    }

    size_t                   pos = crlf + 2;
    std::vector<std::string> args;
    args.reserve(count);

    for (int i = 0; i < count; ++i) {
        if (pos >= buf.size()) return std::nullopt;
        if (buf[pos] != '$') return std::nullopt;
        auto next = buf.find("\r\n", pos);
        if (next == std::string::npos) return std::nullopt;
        int len;
        try {
            len = std::stoi(buf.substr(pos + 1, next - pos - 1));
        } catch (...) {
            buf.clear();
            return std::vector<std::string>{};
        }
        pos = next + 2;
        if (pos + static_cast<size_t>(len) + 2 > buf.size()) return std::nullopt;
        args.push_back(buf.substr(pos, len));
        pos += len + 2;
    }
    buf.erase(0, pos);
    return args;
}

std::optional<std::vector<std::string>> parse(std::string& buf) {
    if (buf.empty()) return std::nullopt;
    if (buf[0] == '*') return parseArray(buf);
    return parseInline(buf);
}

// ── Builders ──────────────────────────────────────────────────────────────────

std::string ok() { return "+OK\r\n"; }
std::string simpleString(const std::string& s) { return "+" + s + "\r\n"; }
std::string error(const std::string& msg) { return "-ERR " + msg + "\r\n"; }
std::string integer(int64_t n) { return ":" + std::to_string(n) + "\r\n"; }
std::string nullBulk() { return "$-1\r\n"; }

std::string bulkString(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

std::string array(const std::vector<std::string>& items) {
    std::string out = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) out += bulkString(item);
    return out;
}

}  // namespace resp
