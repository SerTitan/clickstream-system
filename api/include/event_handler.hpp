#pragma once
#include "clickhouse_client.hpp"
#include "http_request.hpp"
#include "http_response.hpp"

class EventHandler {
public:
    explicit EventHandler(IClickHouseClient& ch);
    void handle_events(const HttpRequest& req, HttpResponse& res);

private:
    IClickHouseClient& ch_;
};
