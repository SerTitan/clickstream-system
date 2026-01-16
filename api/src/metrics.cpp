#include "metrics.hpp"

#include <sstream>

MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry inst;
    return inst;
}

MetricsRegistry::MetricsRegistry() {
    // Pre-create bucket maps lazily in observe
}

void MetricsRegistry::inc_in_flight() {
    std::lock_guard<std::mutex> lk(mu_);
    in_flight_++;
}

void MetricsRegistry::dec_in_flight() {
    std::lock_guard<std::mutex> lk(mu_);
    if (in_flight_ > 0) in_flight_--;
}

void MetricsRegistry::observe_request(const std::string& method,
                                     const std::string& path,
                                     int status,
                                     double duration_seconds) {
    Key k{method, path, status};
    std::lock_guard<std::mutex> lk(mu_);

    req_total_[k]++;
    dur_count_[k]++;
    dur_sum_[k] += duration_seconds;

    for (double b : buckets_) {
        if (duration_seconds <= b) {
            dur_bucket_count_[{k, b}]++;
        }
    }

    dur_bucket_count_[{k, -1.0}]++;
}

static std::string esc_label(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string MetricsRegistry::render_prometheus() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::ostringstream o;

    o << "# HELP http_requests_total Total number of HTTP requests.\n";
    o << "# TYPE http_requests_total counter\n";
    for (const auto& it : req_total_) {
        const Key& k = it.first;
        o << "http_requests_total{method=\"" << esc_label(k.method)
          << "\",path=\"" << esc_label(k.path)
          << "\",status=\"" << k.status
          << "\"} " << it.second << "\n";
    }

    o << "# HELP http_request_duration_seconds Request duration in seconds.\n";
    o << "# TYPE http_request_duration_seconds histogram\n";

    // Buckets
    for (const auto& it : dur_bucket_count_) {
        const Key& k = it.first.first;
        double b = it.first.second;
        o << "http_request_duration_seconds_bucket{method=\"" << esc_label(k.method)
          << "\",path=\"" << esc_label(k.path)
          << "\",status=\"" << k.status << "\",le=\"";
        if (b < 0) o << "+Inf";
        else o << b;
        o << "\"} " << it.second << "\n";
    }

    // sum/count
    for (const auto& it : dur_sum_) {
        const Key& k = it.first;
        double sum = it.second;
        uint64_t cnt = 0;
        auto cIt = dur_count_.find(k);
        if (cIt != dur_count_.end()) cnt = cIt->second;

        o << "http_request_duration_seconds_sum{method=\"" << esc_label(k.method)
          << "\",path=\"" << esc_label(k.path)
          << "\",status=\"" << k.status << "\"} " << sum << "\n";

        o << "http_request_duration_seconds_count{method=\"" << esc_label(k.method)
          << "\",path=\"" << esc_label(k.path)
          << "\",status=\"" << k.status << "\"} " << cnt << "\n";
    }

    o << "# HELP http_in_flight_requests Current in-flight HTTP requests.\n";
    o << "# TYPE http_in_flight_requests gauge\n";
    o << "http_in_flight_requests " << in_flight_ << "\n";

    return o.str();
}
