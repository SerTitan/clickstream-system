#pragma once
#include <cstdint>
#include <string>

struct Url {
  std::string host;
  uint16_t port = 80;
  std::string path = "/";
};

bool parseHttpUrl(const std::string& url, Url* out);

// Sends POST request to http://host:port/path
// Returns true if HTTP status is 2xx/202.
bool httpPostJson(const Url& url, const std::string& body, int* status, std::string* resp, std::string* err);
