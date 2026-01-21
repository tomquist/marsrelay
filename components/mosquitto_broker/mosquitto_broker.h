#pragma once

#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"

extern "C" {
#include "mosq_broker.h"
#include "mosquitto.h"
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
  struct mosquitto *publish_client_{nullptr};
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
