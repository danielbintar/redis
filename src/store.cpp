#include "store.h"

#include <random>

// Rough per-entry bookkeeping overhead (hash node, pointers, std::string headers).
// Not exact — just enough that maxmemory accounting tracks real growth.
static constexpr int64_t kEntryOverhead = 64;

// How many keys to sample when choosing an LRU eviction victim (Redis default).
static constexpr int kMaxMemorySamples = 5;

static int64_t entryBytes(const std::string& key, const std::string& val) {
    return static_cast<int64_t>(key.size() + val.size()) + kEntryOverhead;
}

void Store::eraseEntry(Map::iterator it) {
    usedMemory_ -= entryBytes(it->first, it->second.value);
    // Swap-and-pop from keyIndex_ so it stays dense: move the last pointer into
    // the victim's slot, fix that moved node's recorded slot, then shrink.
    const std::size_t slot        = it->second.slot;
    keyIndex_[slot]               = keyIndex_.back();
    keyIndex_[slot]->second.slot  = slot;
    keyIndex_.pop_back();
    data_.erase(it);
}

bool Store::isExpired(const Entry& e) {
    return e.expiry && Clock::now() >= *e.expiry;
}

Store::Entry* Store::getAlive(const std::string& key) {
    auto it = data_.find(key);
    if (it == data_.end()) return nullptr;
    if (isExpired(it->second)) {
        eraseEntry(it);
        return nullptr;
    }
    return &it->second;
}

bool Store::set(const std::string& key, const std::string& val, int64_t ttl_ms) {
    std::scoped_lock lk(mu_);

    if (maxMemory_ > 0 && usedMemory_ >= maxMemory_) {
        if (policy_ == EvictionPolicy::NoEviction) return false;
        evictToFit();  // allkeys-lru: free room instead of rejecting
    }

    const auto                       now = Clock::now();
    std::optional<Clock::time_point> expiry;
    if (ttl_ms > 0) expiry = now + std::chrono::milliseconds(ttl_ms);

    auto it = data_.find(key);
    if (it == data_.end()) {
        usedMemory_ += entryBytes(key, val);
        Entry e{.value = val, .expiry = expiry, .lastAccess = now, .slot = keyIndex_.size()};
        auto  res = data_.emplace(key, std::move(e));
        keyIndex_.push_back(&*res.first);
    } else {
        // overwrite: adjust by the value-size delta (key size is unchanged)
        usedMemory_ += static_cast<int64_t>(val.size()) -
                       static_cast<int64_t>(it->second.value.size());
        it->second.value      = val;
        it->second.expiry     = expiry;  // SET without EX/PX clears any existing TTL
        it->second.lastAccess = now;
    }
    return true;
}

std::optional<std::string> Store::get(const std::string& key) {
    std::scoped_lock lk(mu_);
    auto*            e = getAlive(key);
    if (e == nullptr) return std::nullopt;
    e->lastAccess = Clock::now();  // reads refresh LRU recency
    return e->value;
}

int64_t Store::del(const std::vector<std::string>& keys) {
    std::scoped_lock lk(mu_);
    int64_t          count = 0;
    for (const auto& k : keys) {
        auto it = data_.find(k);
        if (it == data_.end()) continue;
        // An already-expired-but-not-yet-evicted key is not "deleted" — it was
        // already logically gone. Evict it but don't count it (matches Redis).
        bool expired = isExpired(it->second);
        eraseEntry(it);
        if (!expired) ++count;
    }
    return count;
}

int64_t Store::exists(const std::vector<std::string>& keys) {
    std::scoped_lock lk(mu_);
    int64_t          count = 0;
    for (const auto& k : keys) {
        if (getAlive(k) != nullptr) ++count;
    }
    return count;
}

bool Store::expire(const std::string& key, int64_t ms) {
    std::scoped_lock lk(mu_);
    auto*            e = getAlive(key);
    if (e == nullptr) return false;
    e->expiry = Clock::now() + std::chrono::milliseconds(ms);
    return true;
}

int64_t Store::ttl(const std::string& key) {
    std::scoped_lock lk(mu_);
    auto*            e = getAlive(key);
    if (e == nullptr) return -2;
    if (!e->expiry) return -1;
    auto s = std::chrono::duration_cast<std::chrono::seconds>(*e->expiry - Clock::now()).count();
    return s < 0 ? -2 : s;
}

int64_t Store::pttl(const std::string& key) {
    std::scoped_lock lk(mu_);
    auto*            e = getAlive(key);
    if (e == nullptr) return -2;
    if (!e->expiry) return -1;
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(*e->expiry - Clock::now()).count();
    return ms < 0 ? -2 : ms;
}

bool Store::persist(const std::string& key) {
    std::scoped_lock lk(mu_);
    auto*            e = getAlive(key);
    if (e == nullptr || !e->expiry) return false;
    e->expiry.reset();
    return true;
}

std::vector<std::string> Store::keys() {
    std::scoped_lock         lk(mu_);
    std::vector<std::string> result;
    for (auto& [k, v] : data_) {
        if (!isExpired(v)) result.push_back(k);
    }
    return result;
}

int64_t Store::dbsize() {
    std::scoped_lock lk(mu_);
    int64_t          n = 0;
    for (auto& [k, v] : data_) {
        if (!isExpired(v)) ++n;
    }
    return n;
}

void Store::flushall() {
    std::scoped_lock lk(mu_);
    data_.clear();
    keyIndex_.clear();
    usedMemory_ = 0;
}

void Store::setMaxMemory(int64_t bytes) {
    std::scoped_lock lk(mu_);
    maxMemory_ = bytes;
}

int64_t Store::maxMemory() const {
    std::scoped_lock lk(mu_);
    return maxMemory_;
}

int64_t Store::usedMemory() const {
    std::scoped_lock lk(mu_);
    return usedMemory_;
}

void Store::setEvictionPolicy(EvictionPolicy p) {
    std::scoped_lock lk(mu_);
    policy_ = p;
}

Store::EvictionPolicy Store::evictionPolicy() const {
    std::scoped_lock lk(mu_);
    return policy_;
}

void Store::evictToFit() {
    // allkeys-lru: keep evicting approximated-LRU victims until back under the
    // limit. Each eviction removes one entry and shrinks usedMemory_, so this
    // terminates (worst case when the store is emptied).
    while (usedMemory_ >= maxMemory_ && !data_.empty()) {
        eraseEntry(sampleVictim());
    }
}

Store::Map::iterator Store::sampleVictim() {
    // Approximated LRU: pick the entry with the oldest lastAccess from a small
    // set of candidates. keyIndex_ gives O(1) random access, so this is O(1)
    // regardless of keyspace size (no scan, no iterator advance).
    Map::value_type*  victim = nullptr;
    const std::size_t n      = keyIndex_.size();

    if (n <= static_cast<std::size_t>(kMaxMemorySamples)) {
        // Few keys — just inspect them all; it's already cheap and exact.
        for (auto* node : keyIndex_) {
            if (victim == nullptr || node->second.lastAccess < victim->second.lastAccess)
                victim = node;
        }
    } else {
        static thread_local std::mt19937     rng{std::random_device{}()};
        std::uniform_int_distribution<size_t> dist(0, n - 1);
        victim = keyIndex_[dist(rng)];
        for (int i = 1; i < kMaxMemorySamples; ++i) {
            Map::value_type* cand = keyIndex_[dist(rng)];
            if (cand->second.lastAccess < victim->second.lastAccess) victim = cand;
        }
    }
    return data_.find(victim->first);
}
