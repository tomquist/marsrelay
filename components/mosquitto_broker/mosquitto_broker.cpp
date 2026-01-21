#include "mosquitto_broker.h"

#include "esphome/core/log.h"

namespace esphome {
namespace mosquitto_broker {

static const char *const TAG = "mosquitto_broker";

void MosquittoBroker::setup() {
  ESP_LOGI(TAG, "Starting Mosquitto broker on port %u", this->port_);
  mosquitto_lib_init();

  this->mosq_ = mosquitto_new(nullptr, true, this);
  if (this->mosq_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create mosquitto instance");
    return;
  }

  mosquitto_message_callback_set(this->mosq_, &MosquittoBroker::on_message_callback);

  int rc = mosquitto_loop_start(this->mosq_);
  if (rc != MOSQ_ERR_SUCCESS) {
    ESP_LOGE(TAG, "Failed to start mosquitto loop: %d", rc);
  }

  rc = mosquitto_listen(this->mosq_, nullptr, this->port_, this->max_clients_);
  if (rc != MOSQ_ERR_SUCCESS) {
    ESP_LOGE(TAG, "Failed to listen on port %u: %d", this->port_, rc);
  }
}

void MosquittoBroker::loop() {
  if (this->mosq_ == nullptr) {
    return;
  }

  int rc = mosquitto_loop(this->mosq_, 0, 1);
  if (rc != MOSQ_ERR_SUCCESS) {
    ESP_LOGW(TAG, "Mosquitto loop error: %d", rc);
  }
}

void MosquittoBroker::dump_config() { ESP_LOGCONFIG(TAG, "Mosquitto Broker:"); }

void MosquittoBroker::publish_message(const std::string &topic, const std::string &payload) {
  if (this->mosq_ == nullptr) {
    ESP_LOGW(TAG, "Broker not initialized, skipping publish");
    return;
  }

  int rc = mosquitto_publish(this->mosq_, nullptr, topic.c_str(), payload.size(), payload.data(), 0, false);
  if (rc != MOSQ_ERR_SUCCESS) {
    ESP_LOGW(TAG, "Publish failed for %s: %d", topic.c_str(), rc);
  }
}

void MosquittoBroker::on_message_callback(struct mosquitto *mosq, void *userdata,
                                          const struct mosquitto_message *message) {
  auto *self = static_cast<MosquittoBroker *>(userdata);
  if (self != nullptr) {
    self->handle_message_(message);
  }
}

void MosquittoBroker::handle_message_(const struct mosquitto_message *message) {
  if (message == nullptr || message->topic == nullptr || message->payload == nullptr) {
    return;
  }

  std::string topic(message->topic);
  std::string payload(static_cast<const char *>(message->payload), message->payloadlen);

  for (auto *trigger : this->message_triggers_) {
    trigger->trigger(topic, payload);
  }
}

}  // namespace mosquitto_broker
}  // namespace esphome
