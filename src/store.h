#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Store {
public:
    void set(const std::string& key, const std::string& val, int64_t ttl_ms = 0);
    std::optional<std::string> get(const std::string& key);

    int64_t                  del(const std::vector<std::string>& keys);
    int64_t                  exists(const std::vector<std::string>& keys);
    bool                     expire(const std::string& key, int64_t ms);
    int64_t                  ttl(const std::string& key);  // -1=persist, -2=not found
    int64_t                  pttl(const std::string& key);
    bool                     persist(const std::string& key);
    std::vector<std::string> keys();
    int64_t                  dbsize();
    void                     flushall();

private:
    using Clock = std::chrono::steady_clock;

    struct Entry {
        std::string                      value;
        std::optional<Clock::time_point> expiry;
    };

    std::unordered_map<std::string, Entry> data_;
    mutable std::mutex                     mu_;

    Entry* getAlive(const std::string& key);
};
