#include "http_request.hpp"
#include <sstream>

HttpRequest::HttpRequest(const std::string& raw) {
    std::istringstream ss(raw);
    ss >> method >> path;

    auto pos = raw.find("\r\n\r\n");
    if (pos != std::string::npos) {
        body = raw.substr(pos + 4);
    }
}
