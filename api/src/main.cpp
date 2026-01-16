#include "http_server.hpp"
#include "event_handler.hpp"
#include "clickhouse_client.hpp"
#include "metrics.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static void parse_http_url(const std::string& url, std::string& host_out, int& port_out) {
    std::string u = url;
    const std::string http = "http://";
    if (u.rfind(http, 0) == 0) u = u.substr(http.size());

    auto slash = u.find('/');
    if (slash != std::string::npos) u = u.substr(0, slash);

    auto colon = u.find(':');
    if (colon == std::string::npos) {
        host_out = u;
        port_out = 8123;
        return;
    }

    host_out = u.substr(0, colon);
    port_out = std::atoi(u.substr(colon + 1).c_str());
    if (port_out <= 0) port_out = 8123;
}

int main() {
    const char* port_env = std::getenv("API_PORT");
    int port = port_env ? std::atoi(port_env) : 8081;

    std::string ch_host = "clickhouse";
    int ch_port = 8123;

    if (const char* http_url = std::getenv("CLICKHOUSE_HTTP_URL")) {
        parse_http_url(http_url, ch_host, ch_port);
    } else if (const char* host = std::getenv("CLICKHOUSE_HOST")) {
        ch_host = host;
    }

    std::string ch_db = std::getenv("CLICKHOUSE_DB") ? std::getenv("CLICKHOUSE_DB") : "clickstream";
    std::string ch_user = std::getenv("CLICKHOUSE_USER") ? std::getenv("CLICKHOUSE_USER") : "";
    std::string ch_pass = std::getenv("CLICKHOUSE_PASSWORD") ? std::getenv("CLICKHOUSE_PASSWORD") : "";
    std::string ch_table = std::getenv("CLICKHOUSE_TABLE") ? std::getenv("CLICKHOUSE_TABLE") : "raw_events_dist";

    ClickHouseClient clickhouse(ch_host, ch_port, ch_db, ch_user, ch_pass, ch_table);
    EventHandler handler(clickhouse);

    HttpServer server(port);

    server.add_route("GET", "/health",
        [](const HttpRequest&, HttpResponse& res) {
            res.json(R"({"status":"ok"})");
        });

    server.add_route("GET", "/metrics",
        [](const HttpRequest&, HttpResponse& res) {
            res.status = 200;
            res.text(MetricsRegistry::instance().render_prometheus(), "text/plain; version=0.0.4; charset=utf-8");
        });

    server.add_route("POST", "/events",
        [&](const HttpRequest& req, HttpResponse& res) {
            handler.handle_events(req, res);
        });

    std::cout << "[API] listening on port " << port << std::endl;
    server.start();
}
