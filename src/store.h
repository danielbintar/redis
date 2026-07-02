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
    enum class EvictionPolicy : std::uint8_t { NoEviction, AllKeysLru };

    // Returns false if rejected under the noeviction policy (out of memory).
    // Under allkeys-lru the store first evicts to make room, so it never rejects.
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

    // Memory management
    void           setMaxMemory(int64_t bytes);  // 0 = unlimited
    int64_t        maxMemory() const;
    int64_t        usedMemory() const;
    void           setEvictionPolicy(EvictionPolicy p);
    EvictionPolicy evictionPolicy() const;

private:
    using Clock = std::chrono::steady_clock;

    struct Entry {
        std::string                      value;
        std::optional<Clock::time_point> expiry;
        Clock::time_point                lastAccess;  // for approximated LRU
        std::size_t                      slot{0};     // index into keyIndex_
    };

    using Map = std::unordered_map<std::string, Entry>;

    Map data_;
    // Dense array of pointers to live map nodes, enabling O(1) random sampling
    // for LRU eviction. Kept in sync with data_ via swap-and-pop in eraseEntry().
    // (Pointers to unordered_map elements stay valid across rehash; iterators do not.)
    std::vector<Map::value_type*> keyIndex_;
    mutable std::mutex            mu_;
    int64_t            maxMemory_{0};   // 0 = unlimited
    int64_t            usedMemory_{0};  // running estimate, bytes
    EvictionPolicy     policy_{EvictionPolicy::NoEviction};

    Entry*        getAlive(const std::string& key);
    void          eraseEntry(Map::iterator it);
    static bool   isExpired(const Entry& e);
    void          evictToFit();      // caller must hold mu_
    Map::iterator sampleVictim();    // caller must hold mu_; approximated LRU
};
