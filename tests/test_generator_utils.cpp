#include "test_framework.hpp"
#include "../generator/include/util_time.hpp"
#include "../generator/include/util_csv.hpp"
#include <chrono>

// Tests for util_time functions

TEST(util_time_now_utc_iso8601_format) {
    std::string ts = nowUtcIso8601();

    // Should be in ISO8601 format: YYYY-MM-DDTHH:MM:SSZ
    ASSERT_TRUE(ts.size() >= 20);
    ASSERT_EQ(ts[4], '-');
    ASSERT_EQ(ts[7], '-');
    ASSERT_EQ(ts[10], 'T');
    ASSERT_EQ(ts[13], ':');
    ASSERT_EQ(ts[16], ':');
    ASSERT_EQ(ts.back(), 'Z');
}

TEST(util_time_from_unix_seconds) {
    // Unix timestamp for 2024-01-01 00:00:00 UTC
    long long epoch = 1704067200;
    std::string ts = utcIso8601FromUnixSeconds(epoch);

    ASSERT_CONTAINS(ts, "2024-01-01");
    ASSERT_CONTAINS(ts, "T");
    ASSERT_EQ(ts.back(), 'Z');
}

TEST(util_time_from_unix_seconds_known_date) {
    // Unix timestamp for 2020-01-01
    long long epoch = 1577882445;
    std::string ts = utcIso8601FromUnixSeconds(epoch);

    // Just check it contains valid date format
    ASSERT_CONTAINS(ts, "2020-01-01");
    ASSERT_CONTAINS(ts, "T");
    ASSERT_EQ(ts.back(), 'Z');
}

TEST(util_time_unix_now_seconds) {
    long long now = unixNowSeconds();

    // Should be a reasonable Unix timestamp (after 2020)
    ASSERT_TRUE(now > 1577836800);  // 2020-01-01

    // Should be close to actual current time
    auto sys_now = std::chrono::system_clock::now();
    auto sys_epoch = std::chrono::duration_cast<std::chrono::seconds>(
        sys_now.time_since_epoch()
    ).count();

    // Should be within 1 second of each other
    ASSERT_TRUE(std::abs(now - sys_epoch) <= 1);
}

TEST(util_time_epoch_zero) {
    std::string ts = utcIso8601FromUnixSeconds(0);

    ASSERT_CONTAINS(ts, "1970-01-01");
    ASSERT_CONTAINS(ts, "00:00:00");
}

// Tests for util_csv functions

TEST(csv_escape_no_special_chars) {
    std::string result = csvEscape("hello world");
    ASSERT_EQ(result, "hello world");
}

TEST(csv_escape_with_comma) {
    std::string result = csvEscape("hello,world");

    // Should be quoted
    ASSERT_EQ(result[0], '"');
    ASSERT_EQ(result.back(), '"');
    ASSERT_CONTAINS(result, "hello,world");
}

TEST(csv_escape_with_quotes) {
    std::string result = csvEscape("say \"hello\"");

    // Quotes should be doubled inside
    ASSERT_CONTAINS(result, "\"\"hello\"\"");
}

TEST(csv_escape_with_newline) {
    std::string result = csvEscape("line1\nline2");

    // Should be quoted
    ASSERT_EQ(result[0], '"');
    ASSERT_EQ(result.back(), '"');
}

TEST(csv_escape_empty_string) {
    std::string result = csvEscape("");
    ASSERT_EQ(result, "");
}

TEST(csv_escape_only_quotes) {
    std::string result = csvEscape("\"\"");

    // Should be: """"""""
    ASSERT_EQ(result[0], '"');
    ASSERT_EQ(result.back(), '"');
}

TEST(csv_join_single_column) {
    std::string result = csvJoin({"hello"});
    ASSERT_EQ(result, "hello");
}

TEST(csv_join_multiple_columns) {
    std::string result = csvJoin({"a", "b", "c"});
    ASSERT_EQ(result, "a,b,c");
}

TEST(csv_join_with_escaping) {
    std::string result = csvJoin({"normal", "with,comma", "with\"quote"});

    // Second and third should be escaped
    ASSERT_CONTAINS(result, "normal");
    ASSERT_CONTAINS(result, "\"with,comma\"");
}

TEST(csv_join_empty_vector) {
    std::string result = csvJoin({});
    ASSERT_EQ(result, "");
}

TEST(csv_join_event_like_row) {
    std::string result = csvJoin({
        "uuid-1234",
        "click",
        "2024-01-01T12:00:00Z",
        "2024-01-01T12:00:01Z",
        "sess-1001",
        "1001",
        "192.168.1.1",
        "/catalog",
        "https://google.com",
        "desktop",
        "Mozilla/5.0",
        "#btn",
        "button",
        "Buy",
        "{\"x\":100,\"y\":200}"
    });

    // Result should contain all our data
    ASSERT_CONTAINS(result, "uuid-1234");
    ASSERT_CONTAINS(result, "click");
    ASSERT_CONTAINS(result, "sess-1001");
    ASSERT_CONTAINS(result, "desktop");
    // JSON payload should be quoted (contains comma)
    ASSERT_CONTAINS(result, "\"x\"");
}

TEST(csv_join_json_payload) {
    std::string json = "{\"event_title\":\"click\",\"element_id\":\"#btn\"}";
    std::string result = csvJoin({"id", json});

    // JSON with quotes and commas should be properly escaped
    ASSERT_CONTAINS(result, "id");
}

TEST(csv_escape_carriage_return) {
    std::string result = csvEscape("line1\r\nline2");

    // Should be quoted
    ASSERT_EQ(result[0], '"');
}

TEST(csv_join_unicode) {
    std::string result = csvJoin({"name", "Привет", "日本語"});

    ASSERT_CONTAINS(result, "Привет");
    ASSERT_CONTAINS(result, "日本語");
}
