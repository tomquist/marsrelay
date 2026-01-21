#pragma once

#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/mqtt/mqtt_backend_esp32.h"
#include "esphome/components/mqtt/mqtt_client.h"

extern "C" {
#include "mosq_broker.h"
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome {
namespace mosquitto_broker {

class MosquittoMessageTrigger : public Trigger<std::string, std::string> {
 public:
  explicit MosquittoMessageTrigger(void *parent) : parent_(parent) {}

 private:
  void *parent_;
};

class MosquittoBroker : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_port(uint16_t port) { port_ = port; }
  void set_max_clients(uint16_t max_clients) { max_clients_ = max_clients; }

  void publish_message(const std::string &topic, const std::string &payload);
  void add_message_trigger(MosquittoMessageTrigger *trigger) { this->message_triggers_.push_back(trigger); }

 protected:
  static void broker_task_(void *param);
  static void on_broker_message_callback(char *client, char *topic, char *data, int len, int qos, int retain);

  void handle_message_(char *topic, char *data, int len);
  void ensure_publish_client_();

  uint16_t port_{1883};
  uint16_t max_clients_{10};
  TaskHandle_t broker_task_handle_{nullptr};
  struct mosq_broker_config broker_config_{};
  bool broker_started_{false};
  uint32_t broker_start_at_{0};
  mqtt::MQTTBackendESP32 publish_client_;
  mqtt::MQTTClientState publish_state_{mqtt::MQTT_CLIENT_DISCONNECTED};
  uint32_t connect_begin_{0};
  std::vector<MosquittoMessageTrigger *> message_triggers_;
};

template<typename... Ts> class PublishMessageAction : public Action<Ts...> {
 public:
  PublishMessageAction(MosquittoBroker *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(std::string, topic)
  TEMPLATABLE_VALUE(std::string, payload)

  void play(const Ts &...x) override { this->parent_->publish_message(this->topic_.value(x...), this->payload_.value(x...)); }

 protected:
  MosquittoBroker *parent_;
};

}  // namespace mosquitto_broker
}  // namespace esphome
