#pragma once
#include <string>

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;

    explicit HttpRequest(const std::string& raw);
};
