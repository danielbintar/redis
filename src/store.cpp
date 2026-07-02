#include "store.h"

// Rough per-entry bookkeeping overhead (hash node, pointers, std::string headers).
// Not exact — just enough that maxmemory accounting tracks real growth.
static constexpr int64_t kEntryOverhead = 64;

static int64_t entryBytes(const std::string& key, const std::string& val) {
    return static_cast<int64_t>(key.size() + val.size()) + kEntryOverhead;
}

void Store::eraseEntry(Map::iterator it) {
    usedMemory_ -= entryBytes(it->first, it->second.value);
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

    // noeviction: once at/over the limit, reject all writes
    if (maxMemory_ > 0 && usedMemory_ >= maxMemory_) return false;

    std::optional<Clock::time_point> expiry;
    if (ttl_ms > 0) expiry = Clock::now() + std::chrono::milliseconds(ttl_ms);

    auto it = data_.find(key);
    if (it == data_.end()) {
        usedMemory_ += entryBytes(key, val);
        data_.emplace(key, Entry{.value = val, .expiry = expiry});
    } else {
        // overwrite: adjust by the value-size delta (key size is unchanged)
        usedMemory_ += static_cast<int64_t>(val.size()) -
                       static_cast<int64_t>(it->second.value.size());
        it->second.value  = val;
        it->second.expiry = expiry;  // SET without EX/PX clears any existing TTL
    }
    return true;
}

std::optional<std::string> Store::get(const std::string& key) {
    std::scoped_lock lk(mu_);
    auto*            e = getAlive(key);
    if (e == nullptr) return std::nullopt;
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
