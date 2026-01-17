#include "http_client.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>

bool parseHttpUrl(const std::string& url, Url* out) {
  const std::string prefix = "http://";
  if (url.rfind(prefix, 0) != 0) return false;

  std::string rest = url.substr(prefix.size());
  std::string hostport, path;

  auto slash = rest.find('/');
  if (slash == std::string::npos) {
    hostport = rest;
    path = "/";
  } else {
    hostport = rest.substr(0, slash);
    path = rest.substr(slash);
  }

  std::string host = hostport;
  uint16_t port = 80;

  auto colon = hostport.find(':');
  if (colon != std::string::npos) {
    host = hostport.substr(0, colon);
    std::string ps = hostport.substr(colon + 1);
    long p = std::strtol(ps.c_str(), nullptr, 10);
    if (p < 1 || p > 65535) return false;
    port = static_cast<uint16_t>(p);
  }

  if (host.empty()) return false;

  out->host = host;
  out->port = port;
  out->path = path.empty() ? "/" : path;
  return true;
}

static bool connectTcp(const std::string& host, uint16_t port, int* out_fd, std::string* err) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* res = nullptr;
  int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
  if (rc != 0) {
    if (err) *err = std::string("getaddrinfo failed: ") + gai_strerror(rc);
    return false;
  }

  int fd = -1;
  for (addrinfo* p = res; p; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
  }
  ::freeaddrinfo(res);

  if (fd < 0) {
    if (err) *err = std::string("connect failed: ") + std::strerror(errno);
    return false;
  }

  *out_fd = fd;
  return true;
}

static bool sendAll(int fd, const std::string& data, std::string* err) {
  size_t sent = 0;
  while (sent < data.size()) {
    ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (err) *err = std::string("send failed: ") + std::strerror(errno);
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

static bool recvAll(int fd, std::string* out, std::string* err) {
  out->clear();
  char buf[4096];
  while (true) {
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n == 0) break;
    if (n < 0) {
      if (errno == EINTR) continue;
      if (err) *err = std::string("recv failed: ") + std::strerror(errno);
      return false;
    }
    out->append(buf, buf + n);
  }
  return true;
}

bool httpPostJson(const Url& url, const std::string& body, int* status, std::string* resp, std::string* err) {
  int fd = -1;
  if (!connectTcp(url.host, url.port, &fd, err)) return false;

  std::ostringstream req;
  req << "POST " << url.path << " HTTP/1.1\r\n"
      << "Host: " << url.host << ":" << url.port << "\r\n"
      << "User-Agent: clickstream-generator/1.0\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n\r\n"
      << body;

  if (!sendAll(fd, req.str(), err)) {
    ::close(fd);
    return false;
  }

  std::string raw;
  if (!recvAll(fd, &raw, err)) {
    ::close(fd);
    return false;
  }
  ::close(fd);

  auto p = raw.find("\r\n");
  if (p == std::string::npos) {
    if (err) *err = "invalid http response";
    return false;
  }

  int code = 0;
  {
    std::istringstream iss(raw.substr(0, p));
    std::string httpver;
    iss >> httpver >> code;
  }
  if (status) *status = code;

  auto h_end = raw.find("\r\n\r\n");
  if (resp) *resp = (h_end == std::string::npos) ? "" : raw.substr(h_end + 4);

  return (code >= 200 && code < 300) || code == 202;
}
