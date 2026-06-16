#include "store.h"

#include <array>

Store::Entry* Store::getAlive(const std::string& key) {
    auto it = data_.find(key);
    if (it == data_.end()) return nullptr;
    if (it->second.expiry && Clock::now() >= *it->second.expiry) {
        data_.erase(it);
        return nullptr;
    }
    return &it->second;
}

void Store::set(const std::string& key, const std::string& val, int64_t ttl_ms) {
    std::scoped_lock lk(mu_);
    Entry            e;
    e.value = val;
    if (ttl_ms > 0) e.expiry = Clock::now() + std::chrono::milliseconds(ttl_ms);
    data_[key] = std::move(e);
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
    for (const auto& k : keys) count += static_cast<int64_t>(data_.erase(k));
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
        if (!v.expiry || Clock::now() < *v.expiry) result.push_back(k);
    }
    return result;
}

int64_t Store::dbsize() {
    std::scoped_lock lk(mu_);
    int64_t          n = 0;
    for (auto& [k, v] : data_) {
        if (!v.expiry || Clock::now() < *v.expiry) ++n;
    }
    return n;
}

void Store::flushall() {
    std::scoped_lock lk(mu_);
    data_.clear();
}
