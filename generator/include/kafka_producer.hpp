#pragma once
#include <string>

class KafkaProducer {
public:
  KafkaProducer() = default;
  ~KafkaProducer();

  bool start(const std::string& bootstrap_servers, const std::string& topic, std::string* err);
  bool send(const std::string& payload, std::string* err);
  void flush(int timeout_ms);

private:
  struct Impl;
  Impl* impl_ = nullptr;
};
