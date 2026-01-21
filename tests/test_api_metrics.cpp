#include "test_framework.hpp"
#include "../api/include/metrics.hpp"

// Tests for MetricsRegistry

TEST(metrics_singleton_instance) {
    auto& m1 = MetricsRegistry::instance();
    auto& m2 = MetricsRegistry::instance();
    ASSERT_EQ(&m1, &m2);
}

TEST(metrics_observe_request) {
    auto& m = MetricsRegistry::instance();

    // Observe a request
    m.observe_request("GET", "/health", 200, 0.001);

    std::string output = m.render_prometheus();

    // Should contain http_requests_total metric
    ASSERT_CONTAINS(output, "http_requests_total");
    ASSERT_CONTAINS(output, "method=\"GET\"");
    ASSERT_CONTAINS(output, "path=\"/health\"");
    ASSERT_CONTAINS(output, "status=\"200\"");
}

TEST(metrics_observe_multiple_requests) {
    auto& m = MetricsRegistry::instance();

    m.observe_request("POST", "/events", 200, 0.01);
    m.observe_request("POST", "/events", 200, 0.02);
    m.observe_request("POST", "/events", 400, 0.005);

    std::string output = m.render_prometheus();

    ASSERT_CONTAINS(output, "http_requests_total");
    ASSERT_CONTAINS(output, "method=\"POST\"");
    ASSERT_CONTAINS(output, "path=\"/events\"");
}

TEST(metrics_in_flight) {
    auto& m = MetricsRegistry::instance();

    m.inc_in_flight();
    m.inc_in_flight();

    std::string output1 = m.render_prometheus();
    ASSERT_CONTAINS(output1, "http_in_flight_requests");

    m.dec_in_flight();
    m.dec_in_flight();
}

TEST(metrics_duration_histogram) {
    auto& m = MetricsRegistry::instance();

    m.observe_request("GET", "/test_histogram", 200, 0.003);

    std::string output = m.render_prometheus();

    // Should contain histogram buckets
    ASSERT_CONTAINS(output, "http_request_duration_seconds_bucket");
    ASSERT_CONTAINS(output, "http_request_duration_seconds_sum");
    ASSERT_CONTAINS(output, "http_request_duration_seconds_count");
}

TEST(metrics_prometheus_format) {
    auto& m = MetricsRegistry::instance();

    m.observe_request("GET", "/prometheus_format", 200, 0.05);

    std::string output = m.render_prometheus();

    // Check Prometheus exposition format
    // Lines should not be empty
    ASSERT_TRUE(output.size() > 0);

    // Should have newlines
    ASSERT_CONTAINS(output, "\n");
}

TEST(metrics_different_status_codes) {
    auto& m = MetricsRegistry::instance();

    m.observe_request("POST", "/status_test", 200, 0.01);
    m.observe_request("POST", "/status_test", 201, 0.01);
    m.observe_request("POST", "/status_test", 400, 0.01);
    m.observe_request("POST", "/status_test", 500, 0.01);

    std::string output = m.render_prometheus();

    ASSERT_CONTAINS(output, "status=\"200\"");
    ASSERT_CONTAINS(output, "status=\"201\"");
    ASSERT_CONTAINS(output, "status=\"400\"");
    ASSERT_CONTAINS(output, "status=\"500\"");
}

TEST(metrics_bucket_boundaries) {
    auto& m = MetricsRegistry::instance();

    // Test various durations hitting different buckets
    m.observe_request("GET", "/bucket_test", 200, 0.001);  // < 0.005
    m.observe_request("GET", "/bucket_test", 200, 0.007);  // < 0.01
    m.observe_request("GET", "/bucket_test", 200, 0.03);   // < 0.05
    m.observe_request("GET", "/bucket_test", 200, 0.5);    // < 1.0
    m.observe_request("GET", "/bucket_test", 200, 3.0);    // < 5.0

    std::string output = m.render_prometheus();

    // Should have bucket labels
    ASSERT_CONTAINS(output, "le=\"0.005\"");
    ASSERT_CONTAINS(output, "le=\"0.01\"");
    ASSERT_CONTAINS(output, "le=\"0.05\"");
    ASSERT_CONTAINS(output, "le=\"1\"");
    ASSERT_CONTAINS(output, "le=\"5\"");
    ASSERT_CONTAINS(output, "le=\"+Inf\"");
}
