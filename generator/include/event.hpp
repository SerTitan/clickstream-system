#pragma once
#include <cstdint>
#include <string>

struct ClickstreamEvent {
  std::string type;
  std::string session_id;
  uint64_t user_id = 0;
  std::string url;
  std::string created_at;
  std::string user_agent;
  std::string payload_json;
  std::string referrer;
  std::string device_type;  // desktop, mobile, tablet
  std::string ip;
  std::string country;
};

ClickstreamEvent makeRandomEvent(uint64_t seq, long long base_epoch_seconds, uint32_t days_back);

// JSON object string, compatible with API contract.
std::string eventToJson(const ClickstreamEvent& e);

// CSV header for the dataset file.
std::string csvHeader();
