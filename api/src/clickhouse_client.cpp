#include "clickhouse_client.hpp"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <cctype>

std::string ClickHouseClient::shell_escape_single_quotes(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out;
}

std::string ClickHouseClient::trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

std::vector<std::string> ClickHouseClient::split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(trim(cur));

    std::vector<std::string> cleaned;
    for (auto& x : out) if (!x.empty()) cleaned.push_back(x);
    return cleaned;
}

std::string ClickHouseClient::normalize_base_url(std::string u) {
    u = trim(u);
    if (u.empty()) return u;

    if (u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0) {
        u = "http://" + u;
    }

    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

std::string ClickHouseClient::build_insert_url(const std::string& base_url,
                                               const std::string& db,
                                               const std::string& table) {
    std::string url = base_url
        + "/?database=" + db
        + "&query=INSERT%20INTO%20" + table + "%20FORMAT%20JSONEachRow"
        + "&date_time_input_format=best_effort";
    return url;
}

bool ClickHouseClient::is_retryable_network_error(const std::string& o) {
    const char* patterns[] = {
        "Could not resolve host",
        "Failed to connect",
        "Connection refused",
        "timed out",
        "Timeout",
        "Empty reply from server",
        "Couldn't connect",
        "Connection reset",
        "Network is unreachable",
    };
    for (auto* p : patterns) {
        if (o.find(p) != std::string::npos) return true;
    }
    return false;
}

ClickHouseClient::ClickHouseClient(
    std::string host, int port,
    std::string db,
    std::string user,
    std::string password,
    std::string table
) : user_(std::move(user)), password_(std::move(password))
{
    // 1) Если задан CLICKHOUSE_HTTP_URLS — используем его как failover-список
    if (const char* urls_env = std::getenv("CLICKHOUSE_HTTP_URLS")) {
        auto bases = split_csv(urls_env);
        for (auto& b : bases) {
            auto base_url = normalize_base_url(b);
            if (!base_url.empty()) {
                urls_.push_back(build_insert_url(base_url, db, table));
            }
        }
    }

    // 2) Фолбэк на одиночный host:port если список не задан/пуст
    if (urls_.empty()) {
        std::string base = "http://" + host + ":" + std::to_string(port);
        urls_.push_back(build_insert_url(base, db, table));
    }
}

bool ClickHouseClient::try_insert_once(const std::string& url,
                                      const std::string& json_each_row,
                                      std::string* out)
{
    std::string payload = json_each_row;
    if (payload.empty() || payload.back() != '\n') payload.push_back('\n');

    const std::string data = shell_escape_single_quotes(payload);

    std::ostringstream cmd;
    cmd << "curl -sS --fail-with-body -X POST '" << url << "' "
        << "-H 'Content-Type: application/json' ";

    if (!user_.empty()) {
        cmd << "-u '" << shell_escape_single_quotes(user_ + ":" + password_) << "' ";
    }

    cmd << "--data-binary '" << data << "' 2>&1";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        if (out) *out = "popen() failed";
        return false;
    }

    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) output += buf;

    int rc = pclose(pipe);
    if (out) *out = output;

    return rc == 0;
}

bool ClickHouseClient::insert_raw_event(const std::string& json_each_row, std::string* err_out) {
    if (urls_.empty()) {
        if (err_out) *err_out = "no ClickHouse URLs configured";
        return false;
    }

    std::string last;
    for (size_t attempt = 0; attempt < urls_.size(); ++attempt) {
        const size_t idx = (current_ + attempt) % urls_.size();
        const std::string& url = urls_[idx];

        std::string out;
        if (try_insert_once(url, json_each_row, &out)) {
            current_ = idx;
            return true;
        }

        last = out.empty() ? "curl failed" : out;

        if (!is_retryable_network_error(last)) {
            if (err_out) *err_out = last;
            return false;
        }
    }

    if (err_out) *err_out = last;
    return false;
}
