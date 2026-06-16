#include "../src/store.h"
#include "test.h"
#include <thread>
#include <chrono>

TEST(set_and_get) {
    Store s;
    s.set("key", "value");
    CHECK_EQ(s.get("key").value_or(""), "value");
}

TEST(get_missing_key) {
    Store s;
    CHECK_EQ(s.get("missing").has_value(), false);
}

TEST(set_overwrites) {
    Store s;
    s.set("key", "first");
    s.set("key", "second");
    CHECK_EQ(s.get("key").value_or(""), "second");
}

TEST(del_existing_keys) {
    Store s;
    s.set("a", "1");
    s.set("b", "2");
    CHECK_EQ(s.del({"a", "b"}), 2);
    CHECK_EQ(s.get("a").has_value(), false);
    CHECK_EQ(s.get("b").has_value(), false);
}

TEST(del_missing_key_returns_zero) {
    Store s;
    CHECK_EQ(s.del({"missing"}), 0);
}

TEST(exists) {
    Store s;
    s.set("x", "1");
    CHECK_EQ(s.exists({"x"}), 1);
    CHECK_EQ(s.exists({"x", "x"}), 2); // duplicates counted
    CHECK_EQ(s.exists({"missing"}), 0);
}

TEST(ttl_no_expiry) {
    Store s;
    s.set("k", "v");
    CHECK_EQ(s.ttl("k"), -1);
}

TEST(ttl_missing_key) {
    Store s;
    CHECK_EQ(s.ttl("missing"), -2);
}

TEST(ttl_with_expiry) {
    Store s;
    s.set("k", "v", 5000); // 5s TTL
    CHECK(s.ttl("k") > 0);
    CHECK(s.ttl("k") <= 5);
}

TEST(expire_existing_key) {
    Store s;
    s.set("k", "v");
    CHECK_EQ(s.expire("k", 5000), true);
    CHECK(s.ttl("k") > 0);
}

TEST(expire_missing_key) {
    Store s;
    CHECK_EQ(s.expire("missing", 5000), false);
}

TEST(persist_removes_expiry) {
    Store s;
    s.set("k", "v", 5000);
    CHECK_EQ(s.persist("k"), true);
    CHECK_EQ(s.ttl("k"), -1);
}

TEST(persist_missing_key) {
    Store s;
    CHECK_EQ(s.persist("missing"), false);
}

TEST(persist_key_without_expiry) {
    Store s;
    s.set("k", "v"); // no TTL
    CHECK_EQ(s.persist("k"), false);
}

TEST(pttl_no_expiry) {
    Store s;
    s.set("k", "v");
    CHECK_EQ(s.pttl("k"), -1);
}

TEST(pttl_missing_key) {
    Store s;
    CHECK_EQ(s.pttl("missing"), -2);
}

TEST(pttl_with_expiry) {
    Store s;
    s.set("k", "v", 5000); // 5s TTL
    CHECK(s.pttl("k") > 0);
    CHECK(s.pttl("k") <= 5000);
}

TEST(key_evicted_after_ttl) {
    Store s;
    s.set("k", "v", 50); // 50ms TTL
    CHECK_EQ(s.get("k").has_value(), true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(s.get("k").has_value(), false);
}

TEST(dbsize_and_keys) {
    Store s;
    s.set("a", "1");
    s.set("b", "2");
    CHECK_EQ(s.dbsize(), 2);
    CHECK_EQ(s.keys().size(), size_t(2));
}

TEST(keys_excludes_expired) {
    Store s;
    s.set("live", "1");
    s.set("dead", "2", 50); // 50ms TTL
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // "dead" is expired but still in the map (lazy eviction)
    // keys() and dbsize() must filter it out
    auto ks = s.keys();
    CHECK_EQ(ks.size(), size_t(1));
    CHECK_EQ(ks[0], "live");
    CHECK_EQ(s.dbsize(), 1);
}

TEST(flushall) {
    Store s;
    s.set("a", "1");
    s.set("b", "2");
    s.flushall();
    CHECK_EQ(s.dbsize(), 0);
}

int main() {
    std::cout << "store:\n";
    RUN(set_and_get);
    RUN(get_missing_key);
    RUN(set_overwrites);
    RUN(del_existing_keys);
    RUN(del_missing_key_returns_zero);
    RUN(exists);
    RUN(ttl_no_expiry);
    RUN(ttl_missing_key);
    RUN(ttl_with_expiry);
    RUN(expire_existing_key);
    RUN(expire_missing_key);
    RUN(persist_removes_expiry);
    RUN(persist_missing_key);
    RUN(persist_key_without_expiry);
    RUN(pttl_no_expiry);
    RUN(pttl_missing_key);
    RUN(pttl_with_expiry);
    RUN(key_evicted_after_ttl);
    RUN(dbsize_and_keys);
    RUN(keys_excludes_expired);
    RUN(flushall);
    TEST_RESULTS();
}
