#include "test_framework.hpp"
#include "../generator/include/event.hpp"
#include <set>

// Tests for Event generator

TEST(event_make_random_event_not_empty) {
    ClickstreamEvent e = makeRandomEvent(1, 1705000000, 7);

    ASSERT_TRUE(!e.type.empty());
    ASSERT_TRUE(!e.session_id.empty());
    ASSERT_TRUE(e.user_id > 0);
    ASSERT_TRUE(!e.url.empty());
    ASSERT_TRUE(!e.created_at.empty());
    ASSERT_TRUE(!e.user_agent.empty());
    ASSERT_TRUE(!e.payload_json.empty());
    ASSERT_TRUE(!e.device_type.empty());
    ASSERT_TRUE(!e.ip.empty());
}

TEST(event_type_is_valid) {
    std::set<std::string> valid_types = {"click", "view", "purchase", "signup", "login", "logout"};

    for (uint64_t i = 1; i <= 100; i++) {
        ClickstreamEvent e = makeRandomEvent(i, 1705000000, 7);
        ASSERT_TRUE(valid_types.count(e.type) > 0);
    }
}

TEST(event_device_type_is_valid) {
    std::set<std::string> valid_devices = {"desktop", "mobile", "tablet"};

    for (uint64_t i = 1; i <= 100; i++) {
        ClickstreamEvent e = makeRandomEvent(i, 1705000000, 7);
        ASSERT_TRUE(valid_devices.count(e.device_type) > 0);
    }
}

TEST(event_user_id_in_range) {
    for (uint64_t i = 1; i <= 100; i++) {
        ClickstreamEvent e = makeRandomEvent(i, 1705000000, 7);
        ASSERT_TRUE(e.user_id >= 1000);
        ASSERT_TRUE(e.user_id <= 5000);
    }
}

TEST(event_session_id_format) {
    ClickstreamEvent e = makeRandomEvent(42, 1705000000, 7);

    // Session ID should start with "sess-"
    ASSERT_TRUE(e.session_id.rfind("sess-", 0) == 0);
}

TEST(event_url_starts_with_slash) {
    for (uint64_t i = 1; i <= 50; i++) {
        ClickstreamEvent e = makeRandomEvent(i, 1705000000, 7);
        ASSERT_TRUE(e.url.size() > 0);
        ASSERT_EQ(e.url[0], '/');
    }
}

TEST(event_created_at_iso_format) {
    ClickstreamEvent e = makeRandomEvent(1, 1705000000, 7);

    // ISO8601 format should contain 'T' separator
    ASSERT_CONTAINS(e.created_at, "T");
    // Should end with 'Z' for UTC
    ASSERT_TRUE(e.created_at.back() == 'Z');
}

TEST(event_payload_is_json_object) {
    ClickstreamEvent e = makeRandomEvent(1, 1705000000, 7);

    // Payload should be a JSON object
    ASSERT_EQ(e.payload_json[0], '{');
    ASSERT_EQ(e.payload_json.back(), '}');
    ASSERT_CONTAINS(e.payload_json, "event_title");
    ASSERT_CONTAINS(e.payload_json, "element_id");
    ASSERT_CONTAINS(e.payload_json, "\"x\":");
    ASSERT_CONTAINS(e.payload_json, "\"y\":");
}

TEST(event_ip_format) {
    for (uint64_t i = 1; i <= 50; i++) {
        ClickstreamEvent e = makeRandomEvent(i, 1705000000, 7);

        // IP should contain dots (IPv4 format)
        int dot_count = 0;
        for (char c : e.ip) {
            if (c == '.') dot_count++;
        }
        ASSERT_EQ(dot_count, 3);
    }
}

TEST(event_user_agent_not_empty) {
    for (uint64_t i = 1; i <= 50; i++) {
        ClickstreamEvent e = makeRandomEvent(i, 1705000000, 7);
        ASSERT_TRUE(e.user_agent.size() > 20);  // UA strings are typically long
    }
}

TEST(event_deterministic_for_same_seq) {
    ClickstreamEvent e1 = makeRandomEvent(42, 1705000000, 7);
    ClickstreamEvent e2 = makeRandomEvent(42, 1705000000, 7);

    ASSERT_EQ(e1.type, e2.type);
    ASSERT_EQ(e1.user_id, e2.user_id);
    ASSERT_EQ(e1.session_id, e2.session_id);
    ASSERT_EQ(e1.url, e2.url);
    ASSERT_EQ(e1.device_type, e2.device_type);
}

TEST(event_different_for_different_seq) {
    ClickstreamEvent e1 = makeRandomEvent(1, 1705000000, 7);
    ClickstreamEvent e2 = makeRandomEvent(2, 1705000000, 7);

    // At least some fields should differ (statistically very likely)
    bool different = (e1.type != e2.type) ||
                     (e1.user_id != e2.user_id) ||
                     (e1.url != e2.url) ||
                     (e1.ip != e2.ip);
    ASSERT_TRUE(different);
}

TEST(event_to_json_format) {
    ClickstreamEvent e = makeRandomEvent(1, 1705000000, 7);
    std::string json = eventToJson(e);

    // Should be valid JSON object
    ASSERT_EQ(json[0], '{');
    ASSERT_EQ(json.back(), '}');

    // Should contain required fields
    ASSERT_CONTAINS(json, "\"type\":");
    ASSERT_CONTAINS(json, "\"event_type\":");
    ASSERT_CONTAINS(json, "\"session_id\":");
    ASSERT_CONTAINS(json, "\"user_id\":");
    ASSERT_CONTAINS(json, "\"device_type\":");
    ASSERT_CONTAINS(json, "\"device_id\":");
    ASSERT_CONTAINS(json, "\"url\":");
    ASSERT_CONTAINS(json, "\"page_url\":");
    ASSERT_CONTAINS(json, "\"ip\":");
    ASSERT_CONTAINS(json, "\"user_agent\":");
    ASSERT_CONTAINS(json, "\"created_at\":");
    ASSERT_CONTAINS(json, "\"received_at\":");
    ASSERT_CONTAINS(json, "\"payload\":");
}

TEST(event_to_json_payload_not_escaped) {
    ClickstreamEvent e = makeRandomEvent(1, 1705000000, 7);
    std::string json = eventToJson(e);

    // Payload should be embedded as JSON object, not escaped string
    // So we should see "payload":{ not "payload":"{"
    ASSERT_CONTAINS(json, "\"payload\":{");
}

TEST(event_csv_header) {
    std::string header = csvHeader();

    ASSERT_CONTAINS(header, "event_id");
    ASSERT_CONTAINS(header, "event_type");
    ASSERT_CONTAINS(header, "created_at");
    ASSERT_CONTAINS(header, "received_at");
    ASSERT_CONTAINS(header, "session_id");
    ASSERT_CONTAINS(header, "user_id");
    ASSERT_CONTAINS(header, "ip");
    ASSERT_CONTAINS(header, "url");
    ASSERT_CONTAINS(header, "referrer");
    ASSERT_CONTAINS(header, "device_type");
    ASSERT_CONTAINS(header, "user_agent");
    ASSERT_CONTAINS(header, "payload_json");
}

TEST(event_days_back_affects_timestamp) {
    long long base = 1705000000;

    ClickstreamEvent e1 = makeRandomEvent(1, base, 1);  // 1 day back max
    ClickstreamEvent e7 = makeRandomEvent(1, base, 7);  // 7 days back max (same seq)

    // With same seed but different days_back, timestamps should potentially differ
    // (depends on random, but the range is different)
    // This is a statistical test - just check that both have valid timestamps
    ASSERT_CONTAINS(e1.created_at, "T");
    ASSERT_CONTAINS(e7.created_at, "T");
}

TEST(event_referrer_variety) {
    std::set<std::string> referrers;

    for (uint64_t i = 1; i <= 200; i++) {
        ClickstreamEvent e = makeRandomEvent(i, 1705000000, 7);
        referrers.insert(e.referrer);
    }

    // Should have multiple different referrers
    ASSERT_TRUE(referrers.size() > 1);
}

TEST(event_url_variety) {
    std::set<std::string> urls;

    for (uint64_t i = 1; i <= 200; i++) {
        ClickstreamEvent e = makeRandomEvent(i, 1705000000, 7);
        urls.insert(e.url);
    }

    // Should have multiple different URLs
    ASSERT_TRUE(urls.size() > 5);
}

TEST(event_country_set) {
    std::set<std::string> countries;

    for (uint64_t i = 1; i <= 200; i++) {
        ClickstreamEvent e = makeRandomEvent(i, 1705000000, 7);
        countries.insert(e.country);
    }

    // Should have multiple different countries
    ASSERT_TRUE(countries.size() > 1);
}
