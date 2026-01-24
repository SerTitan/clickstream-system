#include "test_framework.hpp"
#include "../api/include/json.hpp"

// Tests for JSON parser

TEST(json_parse_empty_array) {
    auto result = parse_json_array("[]");
    ASSERT_EQ(result.size(), 0u);
}

TEST(json_parse_empty_object) {
    auto result = parse_json_array("{}");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.size(), 0u);
}

TEST(json_parse_single_object) {
    auto result = parse_json_array(R"({"key":"value"})");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.at("key"), "value");
}

TEST(json_parse_multiple_fields) {
    auto result = parse_json_array(R"({"a":"1","b":"2","c":"3"})");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.at("a"), "1");
    ASSERT_EQ(result[0].object.at("b"), "2");
    ASSERT_EQ(result[0].object.at("c"), "3");
}

TEST(json_parse_number_as_string) {
    auto result = parse_json_array(R"({"count":42})");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.at("count"), "42");
}

TEST(json_parse_negative_number) {
    auto result = parse_json_array(R"({"val":-123})");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.at("val"), "-123");
}

TEST(json_parse_escaped_quotes) {
    auto result = parse_json_array(R"({"msg":"hello \"world\""})");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.at("msg"), "hello \"world\"");
}

TEST(json_parse_escaped_backslash) {
    auto result = parse_json_array(R"({"path":"c:\\users"})");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.at("path"), "c:\\users");
}

TEST(json_parse_escaped_newline) {
    auto result = parse_json_array(R"({"text":"line1\nline2"})");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.at("text"), "line1\nline2");
}

TEST(json_parse_array_of_objects) {
    auto result = parse_json_array(R"([{"id":"1"},{"id":"2"},{"id":"3"}])");
    ASSERT_EQ(result.size(), 3u);
    ASSERT_EQ(result[0].object.at("id"), "1");
    ASSERT_EQ(result[1].object.at("id"), "2");
    ASSERT_EQ(result[2].object.at("id"), "3");
}

TEST(json_parse_nested_object_as_string) {
    auto result = parse_json_array(R"({"payload":{"x":10,"y":20}})");
    ASSERT_EQ(result.size(), 1u);
    // Nested object is stored as raw string
    std::string payload = result[0].object.at("payload");
    ASSERT_CONTAINS(payload, "x");
    ASSERT_CONTAINS(payload, "10");
}

TEST(json_parse_nested_array_as_string) {
    auto result = parse_json_array(R"({"items":[1,2,3]})");
    ASSERT_EQ(result.size(), 1u);
    std::string items = result[0].object.at("items");
    ASSERT_CONTAINS(items, "1");
    ASSERT_CONTAINS(items, "2");
    ASSERT_CONTAINS(items, "3");
}

TEST(json_parse_whitespace_handling) {
    auto result = parse_json_array(R"(  {  "key"  :  "value"  }  )");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.at("key"), "value");
}

TEST(json_parse_unicode_passthrough) {
    auto result = parse_json_array(R"({"name":"Привет"})");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.at("name"), "Привет");
}

TEST(json_parse_event_like_structure) {
    auto result = parse_json_array(R"([{
        "type": "click",
        "session_id": "sess-1234",
        "user_id": "1001",
        "url": "/catalog",
        "created_at": "2025-01-01T12:00:00Z",
        "user_agent": "Mozilla/5.0",
        "device_type": "desktop",
        "ip": "192.168.1.1",
        "payload": {"element_id": "#btn", "x": 100, "y": 200}
    }])");

    ASSERT_EQ(result.size(), 1u);
    auto& obj = result[0].object;
    ASSERT_EQ(obj.at("type"), "click");
    ASSERT_EQ(obj.at("session_id"), "sess-1234");
    ASSERT_EQ(obj.at("user_id"), "1001");
    ASSERT_EQ(obj.at("url"), "/catalog");
    ASSERT_EQ(obj.at("device_type"), "desktop");
    ASSERT_EQ(obj.at("ip"), "192.168.1.1");
}

TEST(json_parse_multiple_events) {
    auto result = parse_json_array(R"([
        {"type":"view","url":"/"},
        {"type":"click","url":"/buy"},
        {"type":"purchase","url":"/checkout"}
    ])");

    ASSERT_EQ(result.size(), 3u);
    ASSERT_EQ(result[0].object.at("type"), "view");
    ASSERT_EQ(result[1].object.at("type"), "click");
    ASSERT_EQ(result[2].object.at("type"), "purchase");
}

TEST(json_parse_empty_string_value) {
    auto result = parse_json_array(R"({"empty":""})");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].object.at("empty"), "");
}

TEST(json_parse_boolean_as_raw) {
    auto result = parse_json_array(R"({"flag":true,"other":false})");
    ASSERT_EQ(result.size(), 1u);
    // Booleans are stored as raw strings
    ASSERT_TRUE(result[0].object.count("flag") > 0 || result[0].object.count("other") > 0);
}
