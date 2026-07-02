#include "commands.h"

#include <cctype>
#include <stdexcept>

#include "resp.h"

static std::string toUpper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string handleCommand(Store& store, const std::vector<std::string>& args) {
    if (args.empty()) return resp::error("empty command");

    const std::string cmd = toUpper(args[0]);

    // ── Server ───────────────────────────────────────────────────────────────
    if (cmd == "PING") {
        return args.size() > 1 ? resp::bulkString(args[1]) : resp::simpleString("PONG");
    }
    if (cmd == "ECHO") {
        if (args.size() != 2) return resp::error("wrong number of arguments for 'echo'");
        return resp::bulkString(args[1]);
    }
    if (cmd == "QUIT") return resp::ok();
    if (cmd == "SELECT") return resp::ok();

    // ── Config ───────────────────────────────────────────────────────────────
    if (cmd == "CONFIG") {
        if (args.size() < 3) return resp::error("wrong number of arguments for 'config'");
        const auto sub   = toUpper(args[1]);
        const auto param = toUpper(args[2]);
        if (sub == "GET") {
            if (param == "MAXMEMORY")
                return resp::array({"maxmemory", std::to_string(store.maxMemory())});
            if (param == "USED-MEMORY")  // not a real Redis config key; handy for testing
                return resp::array({"used-memory", std::to_string(store.usedMemory())});
            return resp::array({});  // unknown param → empty result, like Redis
        }
        if (sub == "SET") {
            if (args.size() != 4) return resp::error("wrong number of arguments for 'config|set'");
            if (param == "MAXMEMORY") {
                try {
                    store.setMaxMemory(std::stoll(args[3]));
                } catch (const std::logic_error&) {
                    return resp::error("argument couldn't be parsed into an integer");
                }
                return resp::ok();
            }
            return resp::error("unknown config parameter '" + args[2] + "'");
        }
        return resp::error("unknown CONFIG subcommand '" + args[1] + "'");
    }

    // ── Strings ──────────────────────────────────────────────────────────────
    if (cmd == "SET") {
        if (args.size() < 3) return resp::error("wrong number of arguments for 'set'");
        int64_t ttl_ms = 0;
        for (size_t i = 3; i < args.size(); ++i) {
            const auto opt = toUpper(args[i]);
            if ((opt == "EX" || opt == "PX") && i + 1 < args.size()) {
                try {
                    int64_t v = std::stoll(args[++i]);
                    ttl_ms    = (opt == "EX") ? v * 1000 : v;
                } catch (const std::logic_error&) {
                    return resp::error("value is not an integer or out of range");
                }
            }
        }
        if (!store.set(args[1], args[2], ttl_ms))
            return resp::error("OOM command not allowed when used memory > 'maxmemory'.");
        return resp::ok();
    }
    if (cmd == "GET") {
        if (args.size() != 2) return resp::error("wrong number of arguments for 'get'");
        auto val = store.get(args[1]);
        return val ? resp::bulkString(*val) : resp::nullBulk();
    }

    // ── Keys ─────────────────────────────────────────────────────────────────
    if (cmd == "DEL") {
        if (args.size() < 2) return resp::error("wrong number of arguments for 'del'");
        return resp::integer(store.del({args.begin() + 1, args.end()}));
    }
    if (cmd == "EXISTS") {
        if (args.size() < 2) return resp::error("wrong number of arguments for 'exists'");
        return resp::integer(store.exists({args.begin() + 1, args.end()}));
    }
    if (cmd == "EXPIRE") {
        if (args.size() != 3) return resp::error("wrong number of arguments for 'expire'");
        try {
            return resp::integer(store.expire(args[1], std::stoll(args[2]) * 1000) ? 1 : 0);
        } catch (const std::logic_error&) {
            return resp::error("value is not an integer or out of range");
        }
    }
    if (cmd == "PEXPIRE") {
        if (args.size() != 3) return resp::error("wrong number of arguments for 'pexpire'");
        try {
            return resp::integer(store.expire(args[1], std::stoll(args[2])) ? 1 : 0);
        } catch (const std::logic_error&) {
            return resp::error("value is not an integer or out of range");
        }
    }
    if (cmd == "TTL") {
        if (args.size() != 2) return resp::error("wrong number of arguments for 'ttl'");
        return resp::integer(store.ttl(args[1]));
    }
    if (cmd == "PTTL") {
        if (args.size() != 2) return resp::error("wrong number of arguments for 'pttl'");
        return resp::integer(store.pttl(args[1]));
    }
    if (cmd == "PERSIST") {
        if (args.size() != 2) return resp::error("wrong number of arguments for 'persist'");
        return resp::integer(store.persist(args[1]) ? 1 : 0);
    }
    if (cmd == "KEYS") return resp::array(store.keys());
    if (cmd == "DBSIZE") return resp::integer(store.dbsize());
    if (cmd == "FLUSHDB" || cmd == "FLUSHALL") {
        store.flushall();
        return resp::ok();
    }

    return resp::error("unknown command '" + args[0] + "'");
}
