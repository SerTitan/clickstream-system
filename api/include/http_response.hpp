#pragma once
#include <string>

struct HttpResponse {
    int status = 200;
    std::string body;

    // HTTP Content-Type header value.
    // Default is JSON since most endpoints in this project return JSON.
    std::string content_type = "application/json";

    void json(const std::string& content);
    // Plain text response helper (useful for /metrics).
    void text(const std::string& content, const std::string& ct = "text/plain; charset=utf-8");
    std::string serialize() const;
};
