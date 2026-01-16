#include "http_response.hpp"

void HttpResponse::json(const std::string& content) {
    content_type = "application/json";
    body = content;
}

void HttpResponse::text(const std::string& content, const std::string& ct) {
    content_type = ct;
    body = content;
}

std::string HttpResponse::serialize() const {
    return "HTTP/1.1 " + std::to_string(status) + " OK\r\n"
           "Content-Type: " + content_type + "\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n"
           + body;
}
