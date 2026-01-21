#include "test_framework.hpp"
#include "clickhouse_client.hpp"
#include <cstdlib>

TEST(ch_client_single_host_url_format) {
    ClickHouseClient client("localhost", 8123, "testdb", "user", "pass", "events");
    std::string err;
    bool result = client.insert_raw_event("{\"test\":1}", &err);
    ASSERT(!result);
    ASSERT(!err.empty());
}

TEST(ch_client_with_env_urls) {
    setenv("CLICKHOUSE_HTTP_URLS", "http://host1:8123, http://host2:8123", 1);
    ClickHouseClient client("ignored", 9999, "testdb", "user", "pass", "events");
    std::string err;
    bool result = client.insert_raw_event("{\"test\":1}", &err);
    ASSERT(!result);
    unsetenv("CLICKHOUSE_HTTP_URLS");
}

TEST(ch_client_empty_json) {
    ClickHouseClient client("localhost", 8123, "testdb", "user", "pass", "events");
    std::string err;
    bool result = client.insert_raw_event("", &err);
    ASSERT(!result);
}

TEST(ch_client_json_with_special_chars) {
    ClickHouseClient client("localhost", 8123, "testdb", "user", "pass", "events");
    std::string json = R"({"message":"test with 'quotes' and \"double\""})";
    std::string err;
    bool result = client.insert_raw_event(json, &err);
    ASSERT(!result);
}

TEST(ch_client_multiline_json) {
    ClickHouseClient client("localhost", 8123, "testdb", "user", "pass", "events");
    std::string json = "{\"a\":1}\n{\"b\":2}\n";
    std::string err;
    bool result = client.insert_raw_event(json, &err);
    ASSERT(!result);
}

TEST(ch_client_host_without_scheme) {
    ClickHouseClient client("myhost", 9000, "db", "", "", "tbl");
    std::string err;
    client.insert_raw_event("{}", &err);
    ASSERT(!err.empty());
}

TEST(ch_client_env_url_with_trailing_slash) {
    setenv("CLICKHOUSE_HTTP_URLS", "http://host:8123///", 1);
    ClickHouseClient client("ignored", 0, "db", "", "", "tbl");
    std::string err;
    client.insert_raw_event("{}", &err);
    ASSERT(!err.empty());
    unsetenv("CLICKHOUSE_HTTP_URLS");
}

TEST(ch_client_env_url_without_scheme) {
    setenv("CLICKHOUSE_HTTP_URLS", "host1:8123, host2:8123", 1);
    ClickHouseClient client("ignored", 0, "db", "", "", "tbl");
    std::string err;
    client.insert_raw_event("{}", &err);
    ASSERT(!err.empty());
    unsetenv("CLICKHOUSE_HTTP_URLS");
}

TEST(ch_client_empty_env_urls) {
    setenv("CLICKHOUSE_HTTP_URLS", "   ,  ,  ", 1);
    ClickHouseClient client("fallback", 8123, "db", "", "", "tbl");
    std::string err;
    client.insert_raw_event("{}", &err);
    ASSERT(!err.empty());
    unsetenv("CLICKHOUSE_HTTP_URLS");
}

TEST(ch_client_handles_connection_refused) {
    ClickHouseClient client("127.0.0.1", 59999, "db", "", "", "tbl");
    std::string err;
    bool result = client.insert_raw_event("{}", &err);
    ASSERT(!result);
    ASSERT(!err.empty());
}

TEST(ch_client_null_error_pointer) {
    ClickHouseClient client("127.0.0.1", 59999, "db", "", "", "tbl");
    bool result = client.insert_raw_event("{}", nullptr);
    ASSERT(!result);
}

TEST(ch_client_with_credentials) {
    ClickHouseClient client("localhost", 8123, "db", "myuser", "mypass", "tbl");
    std::string err;
    client.insert_raw_event("{}", &err);
    ASSERT(!err.empty());
}

TEST(ch_client_empty_credentials) {
    ClickHouseClient client("localhost", 8123, "db", "", "", "tbl");
    std::string err;
    client.insert_raw_event("{}", &err);
    ASSERT(!err.empty());
}

TEST(ch_client_large_payload) {
    ClickHouseClient client("127.0.0.1", 59999, "db", "", "", "tbl");
    std::string large_json = "{\"data\":\"";
    for (int i = 0; i < 10000; i++) large_json += "x";
    large_json += "\"}";
    std::string err;
    bool result = client.insert_raw_event(large_json, &err);
    ASSERT(!result);
}

TEST(ch_client_json_without_trailing_newline) {
    ClickHouseClient client("127.0.0.1", 59999, "db", "", "", "tbl");
    std::string json = "{\"a\":1}";
    std::string err;
    client.insert_raw_event(json, &err);
    ASSERT(!err.empty());
}

TEST(ch_client_json_with_trailing_newline) {
    ClickHouseClient client("127.0.0.1", 59999, "db", "", "", "tbl");
    std::string json = "{\"a\":1}\n";
    std::string err;
    client.insert_raw_event(json, &err);
    ASSERT(!err.empty());
}
