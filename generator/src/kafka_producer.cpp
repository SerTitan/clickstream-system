#include "kafka_producer.hpp"
#include <librdkafka/rdkafka.h>

struct KafkaProducer::Impl {
  rd_kafka_t* rk = nullptr;
  rd_kafka_topic_t* rkt = nullptr;
};

KafkaProducer::~KafkaProducer() {
  if (!impl_) return;
  if (impl_->rkt) rd_kafka_topic_destroy(impl_->rkt);
  if (impl_->rk) rd_kafka_destroy(impl_->rk);
  delete impl_;
  impl_ = nullptr;
}

bool KafkaProducer::start(const std::string& bootstrap_servers, const std::string& topic, std::string* err) {
  impl_ = new Impl();

  char errstr[512];
  rd_kafka_conf_t* conf = rd_kafka_conf_new();

  if (rd_kafka_conf_set(conf, "bootstrap.servers", bootstrap_servers.c_str(),
                       errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
    if (err) *err = errstr;
    rd_kafka_conf_destroy(conf);
    return false;
  }

  impl_->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
  if (!impl_->rk) {
    if (err) *err = errstr;
    return false;
  }

  impl_->rkt = rd_kafka_topic_new(impl_->rk, topic.c_str(), nullptr);
  if (!impl_->rkt) {
    if (err) *err = rd_kafka_err2str(rd_kafka_last_error());
    return false;
  }

  return true;
}

bool KafkaProducer::send(const std::string& payload, std::string* err) {
  if (!impl_ || !impl_->rk || !impl_->rkt) {
    if (err) *err = "kafka producer is not started";
    return false;
  }

  int rc = rd_kafka_produce(
    impl_->rkt,
    RD_KAFKA_PARTITION_UA,
    RD_KAFKA_MSG_F_COPY,
    (void*)payload.data(),
    payload.size(),
    nullptr,
    0,
    nullptr
  );

  if (rc != 0) {
    if (err) *err = rd_kafka_err2str(rd_kafka_last_error());
    return false;
  }

  rd_kafka_poll(impl_->rk, 0);
  return true;
}

void KafkaProducer::flush(int timeout_ms) {
  if (!impl_ || !impl_->rk) return;
  rd_kafka_flush(impl_->rk, timeout_ms);
}
