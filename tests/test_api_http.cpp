#include "test_framework.hpp"
#include "../api/include/http_request.hpp"
#include "../api/include/http_response.hpp"

// Tests for HttpRequest parser

TEST(http_request_parse_get) {
    std::string raw = "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n";
    HttpRequest req(raw);

    ASSERT_EQ(req.method, "GET");
    ASSERT_EQ(req.path, "/health");
    ASSERT_EQ(req.body, "");
}

TEST(http_request_parse_post_with_body) {
    std::string raw = "POST /events HTTP/1.1\r\n"
                      "Host: localhost\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: 13\r\n"
                      "\r\n"
                      "{\"key\":\"val\"}";
    HttpRequest req(raw);

    ASSERT_EQ(req.method, "POST");
    ASSERT_EQ(req.path, "/events");
    ASSERT_EQ(req.body, "{\"key\":\"val\"}");
}

TEST(http_request_parse_path_only) {
    std::string raw = "GET / HTTP/1.1\r\n\r\n";
    HttpRequest req(raw);

    ASSERT_EQ(req.method, "GET");
    ASSERT_EQ(req.path, "/");
}

TEST(http_request_parse_complex_path) {
    std::string raw = "GET /api/v1/events?limit=10&offset=0 HTTP/1.1\r\n\r\n";
    HttpRequest req(raw);

    ASSERT_EQ(req.method, "GET");
    // Path includes query string
    ASSERT_EQ(req.path, "/api/v1/events?limit=10&offset=0");
}

TEST(http_request_parse_empty_body) {
    std::string raw = "POST /events HTTP/1.1\r\nContent-Length: 0\r\n\r\n";
    HttpRequest req(raw);

    ASSERT_EQ(req.method, "POST");
    ASSERT_EQ(req.body, "");
}

TEST(http_request_parse_json_array_body) {
    std::string json = R"([{"type":"click"},{"type":"view"}])";
    std::string raw = "POST /events HTTP/1.1\r\n"
                      "Content-Type: application/json\r\n"
                      "\r\n" + json;
    HttpRequest req(raw);

    ASSERT_EQ(req.body, json);
}

// Tests for HttpResponse serializer

TEST(http_response_default_status) {
    HttpResponse res;
    ASSERT_EQ(res.status, 200);
}

TEST(http_response_json_method) {
    HttpResponse res;
    res.json(R"({"status":"ok"})");

    ASSERT_EQ(res.status, 200);
    ASSERT_EQ(res.body, R"({"status":"ok"})");

    std::string serialized = res.serialize();
    ASSERT_CONTAINS(serialized, "HTTP/1.1 200");
    ASSERT_CONTAINS(serialized, "application/json");
    ASSERT_CONTAINS(serialized, R"({"status":"ok"})");
}

TEST(http_response_text_method) {
    HttpResponse res;
    res.text("Hello, World!", "text/plain");

    ASSERT_EQ(res.body, "Hello, World!");

    std::string serialized = res.serialize();
    ASSERT_CONTAINS(serialized, "text/plain");
    ASSERT_CONTAINS(serialized, "Hello, World!");
}

TEST(http_response_error_status) {
    HttpResponse res;
    res.status = 400;
    res.json(R"({"error":"bad request"})");

    std::string serialized = res.serialize();
    ASSERT_CONTAINS(serialized, "HTTP/1.1 400");
}

TEST(http_response_server_error) {
    HttpResponse res;
    res.status = 500;
    res.body = "Internal Server Error";

    std::string serialized = res.serialize();
    ASSERT_CONTAINS(serialized, "HTTP/1.1 500");
}

TEST(http_response_not_found) {
    HttpResponse res;
    res.status = 404;
    res.body = "Not Found";

    std::string serialized = res.serialize();
    ASSERT_CONTAINS(serialized, "HTTP/1.1 404");
    ASSERT_CONTAINS(serialized, "Not Found");
}

TEST(http_response_content_length) {
    HttpResponse res;
    res.json(R"({"a":1})");

    std::string serialized = res.serialize();
    // Should contain Content-Length header
    ASSERT_CONTAINS(serialized, "Content-Length:");
}

TEST(http_response_empty_body) {
    HttpResponse res;
    res.status = 204;
    res.body = "";

    std::string serialized = res.serialize();
    ASSERT_CONTAINS(serialized, "HTTP/1.1 204");
}

TEST(http_response_serialize_format) {
    HttpResponse res;
    res.json(R"({"test":true})");

    std::string serialized = res.serialize();

    // Check HTTP response format: status line, headers, empty line, body
    ASSERT_CONTAINS(serialized, "\r\n\r\n");
    size_t body_start = serialized.find("\r\n\r\n") + 4;
    std::string body = serialized.substr(body_start);
    ASSERT_EQ(body, R"({"test":true})");
}
