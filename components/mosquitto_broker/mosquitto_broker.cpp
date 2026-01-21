#include "mosquitto_broker.h"

#include "esphome/core/log.h"

namespace esphome {
namespace mosquitto_broker {

static const char *const TAG = "mosquitto_broker";
static MosquittoBroker *global_broker = nullptr;

void MosquittoBroker::setup() {
  ESP_LOGI(TAG, "Starting Mosquitto broker on port %u", this->port_);
  mosquitto_lib_init();

  this->broker_config_.host = "0.0.0.0";
  this->broker_config_.port = this->port_;
  this->broker_config_.tls_cfg = nullptr;
  this->broker_config_.handle_message_cb = &MosquittoBroker::on_broker_message_callback;
  this->broker_config_.handle_connect_cb = nullptr;

  global_broker = this;
  if (this->broker_task_handle_ == nullptr) {
    xTaskCreate(&MosquittoBroker::broker_task_, "mosq_broker", 4096, this, 5, &this->broker_task_handle_);
  }

  this->ensure_publish_client_();
}

void MosquittoBroker::loop() {
  if (this->publish_client_ == nullptr) {
    return;
  }

  int rc = mosquitto_loop(this->publish_client_, 0, 1);
  if (rc != MOSQ_ERR_SUCCESS) {
    ESP_LOGW(TAG, "Mosquitto loop error: %d", rc);
    this->ensure_publish_client_();
  }
}

void MosquittoBroker::dump_config() { ESP_LOGCONFIG(TAG, "Mosquitto Broker:"); }

void MosquittoBroker::publish_message(const std::string &topic, const std::string &payload) {
  if (this->publish_client_ == nullptr) {
    ESP_LOGW(TAG, "Broker not initialized, skipping publish");
    return;
  }

  int rc = mosquitto_publish(this->publish_client_, nullptr, topic.c_str(), payload.size(), payload.data(), 0, false);
  if (rc != MOSQ_ERR_SUCCESS) {
    ESP_LOGW(TAG, "Publish failed for %s: %d", topic.c_str(), rc);
  }
}

void MosquittoBroker::broker_task_(void *param) {
  auto *self = static_cast<MosquittoBroker *>(param);
  if (self == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  mosq_broker_run(&self->broker_config_);
  vTaskDelete(nullptr);
}

void MosquittoBroker::on_broker_message_callback(char *client, char *topic, char *data, int len, int qos, int retain) {
  (void) client;
  (void) qos;
  (void) retain;
  if (topic == nullptr || data == nullptr) {
    return;
  }
  if (global_broker != nullptr) {
    global_broker->handle_message_(topic, data, len);
  }
}

void MosquittoBroker::handle_message_(char *topic, char *data, int len) {
  std::string topic_str(topic);
  std::string payload(data, len);

  for (auto *trigger : this->message_triggers_) {
    trigger->trigger(topic_str, payload);
  }
}

void MosquittoBroker::ensure_publish_client_() {
  if (this->publish_client_ == nullptr) {
    this->publish_client_ = mosquitto_new("marsrelay-publish", true, nullptr);
  }
  if (this->publish_client_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create publish client");
    return;
  }
  int rc = mosquitto_connect(this->publish_client_, "127.0.0.1", this->port_, 60);
  if (rc != MOSQ_ERR_SUCCESS) {
    ESP_LOGW(TAG, "Publish client connect failed: %d", rc);
  }
}

}  // namespace mosquitto_broker
}  // namespace esphome
