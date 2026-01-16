#include "http_server.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "metrics.hpp"

#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <chrono>

HttpServer::HttpServer(int port) : port_(port) {}

void HttpServer::add_route(
    const std::string& method,
    const std::string& path,
    Handler handler) {
    routes_[method + " " + path] = std::move(handler);
}

void HttpServer::start() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 16);

    while (true) {
        int client = accept(server_fd, nullptr, nullptr);

        char buffer[8192]{};
        const ssize_t n = ::read(client, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            ::close(client);
            continue;
        }
        buffer[n] = '\0';

        HttpRequest req(buffer);
        HttpResponse res;

        auto start_ts = std::chrono::steady_clock::now();
        MetricsRegistry::instance().inc_in_flight();

        auto key = req.method + " " + req.path;
        if (routes_.count(key)) {
            routes_[key](req, res);
        } else {
            res.status = 404;
            res.body = "Not Found";
        }

        auto end_ts = std::chrono::steady_clock::now();
        std::chrono::duration<double> dt = end_ts - start_ts;
        MetricsRegistry::instance().dec_in_flight();
        MetricsRegistry::instance().observe_request(req.method, req.path, res.status, dt.count());

        std::string response = res.serialize();
        const ssize_t w = ::write(client, response.c_str(), response.size());
        (void)w;
        ::close(client);
    }
}
