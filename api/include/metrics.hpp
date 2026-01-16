#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

class MetricsRegistry {
public:
    static MetricsRegistry& instance();

    void observe_request(const std::string& method,
                         const std::string& path,
                         int status,
                         double duration_seconds);

    void inc_in_flight();
    void dec_in_flight();

    std::string render_prometheus() const;

private:
    MetricsRegistry();

    struct Key {
        std::string method;
        std::string path;
        int status;

        bool operator<(const Key& o) const {
            if (method != o.method) return method < o.method;
            if (path != o.path) return path < o.path;
            return status < o.status;
        }
    };

    const std::vector<double> buckets_{0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};

    mutable std::mutex mu_;
    std::map<Key, uint64_t> req_total_;
    std::map<Key, uint64_t> dur_count_;
    std::map<Key, double> dur_sum_;
    std::map<std::pair<Key, double>, uint64_t> dur_bucket_count_;
    uint64_t in_flight_{0};
};
