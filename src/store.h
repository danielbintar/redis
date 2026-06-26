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
    // Returns false if rejected under the noeviction policy (out of memory).
    bool                       set(const std::string& key, const std::string& val,
                                   int64_t ttl_ms = 0);
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

    // Memory management (noeviction policy)
    void    setMaxMemory(int64_t bytes);  // 0 = unlimited
    int64_t maxMemory() const;
    int64_t usedMemory() const;

private:
    using Clock = std::chrono::steady_clock;

    struct Entry {
        std::string                      value;
        std::optional<Clock::time_point> expiry;
    };

    using Map = std::unordered_map<std::string, Entry>;

    Map                data_;
    mutable std::mutex mu_;
    int64_t            maxMemory_{0};   // 0 = unlimited
    int64_t            usedMemory_{0};  // running estimate, bytes

    Entry* getAlive(const std::string& key);
    void   eraseEntry(Map::iterator it);
};
