#include "test_framework.hpp"
#include "mock_clickhouse.hpp"
#include "event_handler.hpp"
#include "event_utils.hpp"
#include "http_request.hpp"
#include "http_response.hpp"

using namespace event_utils;

static std::string make_valid_event() {
    return R"([{"type":"view","session_id":"s1","user_id":"1","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"Mozilla","payload":{"el":"btn"},"referrer":"","device_type":"desktop","ip":"127.0.0.1"}])";
}

TEST(handler_valid_single_event) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + make_valid_event());
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 200);
    ASSERT_EQ(ch.insert_call_count, 1);
}

TEST(handler_valid_multiple_events) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"view","session_id":"s1","user_id":"1","url":"/a","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{},"device_type":"mobile","ip":"1.1.1.1"},{"type":"click","session_id":"s2","user_id":"2","url":"/b","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{"x":1},"device_type":"desktop","ip":"2.2.2.2"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 200);
    ASSERT_EQ(ch.insert_call_count, 2);
}

TEST(handler_missing_session_id) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"view","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{},"device_type":"desktop","ip":"1.1.1.1"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 400);
    ASSERT_EQ(ch.insert_call_count, 0);
}

TEST(handler_missing_payload) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"view","session_id":"s1","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","device_type":"desktop","ip":"1.1.1.1"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 400);
}

TEST(handler_invalid_payload_not_json) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"view","session_id":"s1","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":"not json","device_type":"desktop","ip":"1.1.1.1"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 400);
}

TEST(handler_invalid_event_type) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"invalid_type","session_id":"s1","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{},"device_type":"desktop","ip":"1.1.1.1"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 400);
}

TEST(handler_unknown_field) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"view","session_id":"s1","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{},"device_type":"desktop","ip":"1.1.1.1","unknown_field":"value"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 400);
}

TEST(handler_clickhouse_insert_fails) {
    MockClickHouseClient ch;
    ch.insert_result = false;
    ch.insert_error = "connection refused";
    EventHandler handler(ch);
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + make_valid_event());
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 502);
}

TEST(handler_partial_insert_failure) {
    MockClickHouseClient ch;
    int call_num = 0;
    ch.insert_callback = [&call_num](const std::string&) { return ++call_num <= 1; };
    EventHandler handler(ch);
    std::string body = R"([{"type":"view","session_id":"s1","user_id":"1","url":"/a","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{},"device_type":"desktop","ip":"1.1.1.1"},{"type":"view","session_id":"s2","user_id":"2","url":"/b","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{},"device_type":"desktop","ip":"1.1.1.1"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 502);
}

TEST(handler_empty_array) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n[]");
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 200);
    ASSERT_EQ(ch.insert_call_count, 0);
}

TEST(handler_event_type_alias_page_view) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"page_view","session_id":"s1","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{},"device_type":"desktop","ip":"1.1.1.1"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 200);
}

TEST(handler_event_type_uppercase) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"VIEW","session_id":"s1","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{},"device_type":"desktop","ip":"1.1.1.1"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 200);
}

TEST(handler_missing_device_type) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"view","session_id":"s1","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{},"ip":"1.1.1.1"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 400);
}

TEST(handler_missing_ip) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"view","session_id":"s1","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":{},"device_type":"desktop"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 400);
}

TEST(handler_payload_array) {
    MockClickHouseClient ch;
    EventHandler handler(ch);
    std::string body = R"([{"type":"view","session_id":"s1","url":"/test","created_at":"2024-01-01T00:00:00Z","user_agent":"UA","payload":[1,2,3],"device_type":"desktop","ip":"1.1.1.1"}])";
    HttpRequest req("POST /events HTTP/1.1\r\n\r\n" + body);
    HttpResponse res;
    handler.handle_events(req, res);
    ASSERT_EQ(res.status, 200);
}

// event_utils tests
TEST(event_utils_normalize_event_type_pageview) {
    ASSERT_EQ(normalize_event_type("page_view"), "view");
    ASSERT_EQ(normalize_event_type("pageview"), "view");
    ASSERT_EQ(normalize_event_type("view_page"), "view");
}

TEST(event_utils_normalize_event_type_signup_variants) {
    ASSERT_EQ(normalize_event_type("sign_up"), "signup");
    ASSERT_EQ(normalize_event_type("register"), "signup");
    ASSERT_EQ(normalize_event_type("registration"), "signup");
}

TEST(event_utils_normalize_event_type_login_logout) {
    ASSERT_EQ(normalize_event_type("sign_in"), "login");
    ASSERT_EQ(normalize_event_type("sign_out"), "logout");
}

TEST(event_utils_normalize_event_type_uppercase) {
    ASSERT_EQ(normalize_event_type("VIEW"), "view");
    ASSERT_EQ(normalize_event_type("CLICK"), "click");
    ASSERT_EQ(normalize_event_type("PAGE_VIEW"), "view");
}

TEST(event_utils_trim_ws_basic) {
    ASSERT_EQ(trim_ws("  hello  "), "hello");
    ASSERT_EQ(trim_ws("\n\ttest\r\n"), "test");
    ASSERT_EQ(trim_ws(""), "");
    ASSERT_EQ(trim_ws("   "), "");
}

TEST(event_utils_looks_like_json_object) {
    ASSERT(looks_like_json_text("{}"));
    ASSERT(looks_like_json_text("  {\"a\":1}  "));
    ASSERT(looks_like_json_text("[]"));
    ASSERT(looks_like_json_text("[1,2,3]"));
    ASSERT(!looks_like_json_text("hello"));
    ASSERT(!looks_like_json_text(""));
    ASSERT(!looks_like_json_text("   "));
}

TEST(event_utils_json_escape_special_chars) {
    ASSERT_EQ(json_escape("hello"), "hello");
    ASSERT_EQ(json_escape("a\"b"), "a\\\"b");
    ASSERT_EQ(json_escape("a\\b"), "a\\\\b");
    ASSERT_EQ(json_escape("a\nb"), "a\\nb");
    ASSERT_EQ(json_escape("a\tb"), "a\\tb");
}

TEST(event_utils_unescape_backslash_json) {
    ASSERT_EQ(unescape_backslash_json("hello"), "hello");
    ASSERT_EQ(unescape_backslash_json("a\\\"b"), "a\"b");
    ASSERT_EQ(unescape_backslash_json("a\\\\b"), "a\\b");
    ASSERT_EQ(unescape_backslash_json("a\\nb"), "a\nb");
    ASSERT_EQ(unescape_backslash_json("a\\tb"), "a\tb");
    ASSERT_EQ(unescape_backslash_json("a\\rb"), "a\rb");
    ASSERT_EQ(unescape_backslash_json("a\\xb"), "a\\xb");
}

TEST(event_utils_get_or) {
    std::map<std::string, std::string> m = {{"a", "1"}, {"b", "2"}};
    ASSERT_EQ(get_or(m, "a", "def"), "1");
    ASSERT_EQ(get_or(m, "c", "def"), "def");
}

TEST(event_utils_allowed_event_types) {
    ASSERT(allowed_event_types().count("click") == 1);
    ASSERT(allowed_event_types().count("view") == 1);
    ASSERT(allowed_event_types().count("purchase") == 1);
    ASSERT(allowed_event_types().count("invalid") == 0);
}

TEST(event_utils_allowed_keys) {
    ASSERT(allowed_keys().count("session_id") == 1);
    ASSERT(allowed_keys().count("event_type") == 1);
    ASSERT(allowed_keys().count("unknown_key") == 0);
}
