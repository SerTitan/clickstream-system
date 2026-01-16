#include "event_handler.hpp"
#include "event_utils.hpp"
#include "json.hpp"
#include <map>
#include <string>

using namespace event_utils;

EventHandler::EventHandler(IClickHouseClient& ch) : ch_(ch) {}

void EventHandler::handle_events(const HttpRequest& req, HttpResponse& res) {
    auto events = parse_json_array(req.body);

    std::vector<std::string> rows;
    std::vector<std::string> errors;
    rows.reserve(events.size());

    for (const auto& e : events) {
        for (const auto& kv : e.object) {
            if (allowed_keys().count(kv.first) == 0) {
                errors.push_back("unknown field: " + kv.first);
                break;
            }
        }
        if (!errors.empty()) continue;

        const std::string user_id    = get_or(e.object, "user_id", "");
        const std::string session_id = get_or(e.object, "session_id", "");
        const std::string device_id  = get_or(e.object, "device_id", "unknown");
        const std::string device_type = get_or(e.object, "device_type", "");

        std::string event_type = get_or(e.object, "event_type", "");
        if (event_type.empty()) event_type = get_or(e.object, "type", "");

        event_type = normalize_event_type(event_type);
        if (event_type.empty() || allowed_event_types().count(event_type) == 0) {
            errors.push_back("invalid event_type (allowed: click/view/purchase/signup/login/logout, plus page_view as alias)");
            continue;
        }

        std::string page_url = get_or(e.object, "page_url", "");
        if (page_url.empty()) page_url = get_or(e.object, "url", "");

        const std::string event_time = get_or(e.object, "created_at", "");
        const std::string referrer   = get_or(e.object, "referrer", "");
        const std::string user_agent = get_or(e.object, "user_agent", "");
        const std::string ip         = get_or(e.object, "ip", "");

        std::string props_json = trim_ws(get_or(e.object, "payload", ""));
        if (props_json.empty()) {
            errors.push_back("missing required field: payload (must be JSON object)");
            continue;
        }
        if (!looks_like_json_text(props_json)) {
            errors.push_back("payload must be a JSON object (example: \"payload\":{...})");
            continue;
        }

        if (session_id.empty() || event_time.empty() || page_url.empty() || user_agent.empty() || ip.empty() || device_type.empty()) {
            errors.push_back("missing required fields (session_id/created_at/url/user_agent/ip/device_type)");
            continue;
        }

        std::string raw_payload = "{";
        raw_payload += "\"event_type\":\"" + json_escape(event_type) + "\"";
        raw_payload += ",\"created_at\":\"" + json_escape(event_time) + "\"";
        raw_payload += ",\"session_id\":\"" + json_escape(session_id) + "\"";
        raw_payload += ",\"user_id\":\"" + json_escape(user_id) + "\"";
        raw_payload += ",\"ip\":\"" + json_escape(ip) + "\"";
        raw_payload += ",\"url\":\"" + json_escape(page_url) + "\"";
        raw_payload += ",\"referrer\":\"" + json_escape(referrer) + "\"";
        raw_payload += ",\"device_type\":\"" + json_escape(device_type) + "\"";
        raw_payload += ",\"user_agent\":\"" + json_escape(user_agent) + "\"";
        raw_payload += ",\"payload\":" + props_json;
        raw_payload += "}";

        std::string row = "{";
        row += "\"event_time\":\"" + json_escape(event_time) + "\"";
        row += ",\"user_id\":\"" + json_escape(user_id) + "\"";
        row += ",\"session_id\":\"" + json_escape(session_id) + "\"";
        row += ",\"device_id\":\"" + json_escape(device_id) + "\"";
        row += ",\"event_type\":\"" + json_escape(event_type) + "\"";
        row += ",\"page_url\":\"" + json_escape(page_url) + "\"";
        row += ",\"referrer\":\"" + json_escape(referrer) + "\"";
        row += ",\"user_agent\":\"" + json_escape(user_agent) + "\"";
        row += ",\"ip\":\"" + json_escape(ip) + "\"";
        row += ",\"props_json\":\"" + json_escape(props_json) + "\"";
        row += ",\"raw_payload\":\"" + json_escape(raw_payload) + "\"";
        row += "}";

        rows.push_back(std::move(row));
    }

    if (!errors.empty()) {
        res.status = 400;
        std::string msg = "{\"status\":\"error\",\"error\":\"validation_failed\",\"details\":[";
        const size_t n = std::min<size_t>(errors.size(), 5);
        for (size_t i = 0; i < n; ++i) {
            if (i) msg += ",";
            msg += "\"" + json_escape(errors[i]) + "\"";
        }
        msg += "]}";
        res.json(msg);
        return;
    }

    int ok = 0;
    std::string last_err;
    for (const auto& row : rows) {
        std::string err;
        if (ch_.insert_raw_event(row, &err)) {
            ok++;
        } else {
            last_err = err;
            break;
        }
    }

    if (ok != static_cast<int>(rows.size())) {
        res.status = 502;
        res.json("{\"status\":\"error\",\"error\":\"clickhouse_insert_failed\",\"accepted\":" + std::to_string(ok) +
                 ",\"total\":" + std::to_string(rows.size()) +
                 ",\"last_error\":\"" + json_escape(last_err) + "\"}");
        return;
    }

    res.json("{\"status\":\"ok\",\"accepted\":" + std::to_string(ok) + "}");
}
