#include "mosquitto_broker.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mosquitto_broker {

static const char *const TAG = "mosquitto_broker";
static MosquittoBroker *global_broker = nullptr;

void MosquittoBroker::setup() {
  ESP_LOGI(TAG, "Starting Mosquitto broker on port %u", this->port_);
  static char broker_host[] = "0.0.0.0";
  this->broker_config_.host = broker_host;
  this->broker_config_.port = this->port_;
  this->broker_config_.tls_cfg = nullptr;
  this->broker_config_.handle_message_cb = &MosquittoBroker::on_broker_message_callback;

  global_broker = this;
  if (this->broker_task_handle_ == nullptr) {
    xTaskCreate(&MosquittoBroker::broker_task_, "mosq_broker", 4096, this, 5, &this->broker_task_handle_);
  }

  this->publish_client_.set_on_disconnect([this](mqtt::MQTTClientDisconnectReason reason) {
    (void) reason;
    this->publish_state_ = mqtt::MQTT_CLIENT_DISCONNECTED;
  });
  this->ensure_publish_client_();
}

void MosquittoBroker::loop() {
  this->publish_client_.loop();
  if (!this->publish_client_.connected()) {
    this->publish_state_ = mqtt::MQTT_CLIENT_DISCONNECTED;
    if (esphome::millis() - this->connect_begin_ > 5000) {
      this->ensure_publish_client_();
    }
    return;
  }

  if (this->publish_state_ != mqtt::MQTT_CLIENT_CONNECTED) {
    this->publish_state_ = mqtt::MQTT_CLIENT_CONNECTED;
    ESP_LOGI(TAG, "Publish client connected");
  }
}

void MosquittoBroker::dump_config() { ESP_LOGCONFIG(TAG, "Mosquitto Broker:"); }

void MosquittoBroker::publish_message(const std::string &topic, const std::string &payload) {
  if (this->publish_state_ != mqtt::MQTT_CLIENT_CONNECTED || !this->publish_client_.connected()) {
    this->ensure_publish_client_();
  }
  if (!this->publish_client_.connected()) {
    ESP_LOGW(TAG, "Publish client not connected, skipping publish");
    return;
  }
  if (!this->publish_client_.publish(topic.c_str(), payload.c_str(), payload.length(), 0, false)) {
    ESP_LOGW(TAG, "Publish failed for %s", topic.c_str());
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
  if (this->publish_state_ == mqtt::MQTT_CLIENT_CONNECTING) {
    return;
  }
  this->publish_client_.disconnect();
  this->publish_client_.set_client_id("marsrelay-publish");
  this->publish_client_.set_clean_session(true);
  this->publish_client_.set_credentials(nullptr, nullptr);
  this->publish_client_.set_server("127.0.0.1", this->port_);
  this->publish_client_.connect();
  this->publish_state_ = mqtt::MQTT_CLIENT_CONNECTING;
  this->connect_begin_ = esphome::millis();
}

}  // namespace mosquitto_broker
}  // namespace esphome
