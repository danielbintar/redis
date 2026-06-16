#include "../src/resp.h"
#include "test.h"

TEST(parse_array_command) {
    std::string buf = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    auto result = resp::parse(buf);
    CHECK(result.has_value());
    CHECK_EQ(result->size(), size_t(3));
    CHECK_EQ((*result)[0], "SET");
    CHECK_EQ((*result)[1], "foo");
    CHECK_EQ((*result)[2], "bar");
    CHECK_EQ(buf, ""); // buffer fully consumed
}

TEST(parse_inline_command) {
    std::string buf = "PING\r\n";
    auto result = resp::parse(buf);
    CHECK(result.has_value());
    CHECK_EQ((*result)[0], "PING");
}

TEST(parse_incomplete_returns_nullopt) {
    std::string buf = "*3\r\n$3\r\nSET\r\n";
    auto result = resp::parse(buf);
    CHECK(!result.has_value());
}

TEST(parse_multiple_commands_in_buffer) {
    std::string buf = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
    auto r1 = resp::parse(buf);
    auto r2 = resp::parse(buf);
    CHECK(r1.has_value());
    CHECK(r2.has_value());
    CHECK_EQ(buf, "");
}

TEST(builder_ok) {
    CHECK_EQ(resp::ok(), "+OK\r\n");
}

TEST(builder_simple_string) {
    CHECK_EQ(resp::simpleString("PONG"), "+PONG\r\n");
}

TEST(builder_error) {
    CHECK_EQ(resp::error("bad"), "-ERR bad\r\n");
}

TEST(builder_integer) {
    CHECK_EQ(resp::integer(42), ":42\r\n");
    CHECK_EQ(resp::integer(-1), ":-1\r\n");
}

TEST(builder_bulk_string) {
    CHECK_EQ(resp::bulkString("hello"), "$5\r\nhello\r\n");
}

TEST(builder_null_bulk) {
    CHECK_EQ(resp::nullBulk(), "$-1\r\n");
}

TEST(builder_array) {
    CHECK_EQ(resp::array({"foo", "bar"}), "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
}

TEST(builder_empty_array) {
    CHECK_EQ(resp::array({}), "*0\r\n");
}

TEST(parse_empty_array) {
    std::string buf = "*0\r\n";
    auto result = resp::parse(buf);
    CHECK(result.has_value());
    CHECK_EQ(result->size(), size_t(0));
}

TEST(parse_malformed_array_count) {
    std::string buf = "*abc\r\n";
    auto result = resp::parse(buf);
    // should not crash; returns empty vector and clears buf
    CHECK(result.has_value());
}

TEST(parse_malformed_bulk_length) {
    std::string buf = "*1\r\n$abc\r\nfoo\r\n";
    auto result = resp::parse(buf);
    CHECK(result.has_value());
}

TEST(parse_truncated_before_bulk_marker) {
    // buffer ends right at the count, no bulk string follows
    std::string buf = "*2\r\n";
    auto result = resp::parse(buf);
    CHECK(!result.has_value());
}

TEST(parse_wrong_bulk_marker) {
    // ':' instead of '$'
    std::string buf = "*1\r\n:42\r\n";
    auto result = resp::parse(buf);
    CHECK(!result.has_value());
}

TEST(parse_truncated_bulk_data) {
    // length says 5 but only 3 bytes of data present
    std::string buf = "*1\r\n$5\r\nhel";
    auto result = resp::parse(buf);
    CHECK(!result.has_value());
}

TEST(parse_inline_no_newline) {
    std::string buf = "PING"; // no \r\n yet
    auto result = resp::parse(buf);
    CHECK(!result.has_value());
}

int main() {
    std::cout << "resp:\n";
    RUN(parse_array_command);
    RUN(parse_inline_command);
    RUN(parse_incomplete_returns_nullopt);
    RUN(parse_multiple_commands_in_buffer);
    RUN(parse_empty_array);
    RUN(parse_malformed_array_count);
    RUN(parse_malformed_bulk_length);
    RUN(parse_truncated_before_bulk_marker);
    RUN(parse_wrong_bulk_marker);
    RUN(parse_truncated_bulk_data);
    RUN(parse_inline_no_newline);
    RUN(builder_ok);
    RUN(builder_simple_string);
    RUN(builder_error);
    RUN(builder_integer);
    RUN(builder_bulk_string);
    RUN(builder_null_bulk);
    RUN(builder_array);
    RUN(builder_empty_array);
    TEST_RESULTS();
}
