#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/mqtt/mqtt_backend_esp32.h"
#include "esphome/components/mqtt/mqtt_client.h"

extern "C" {
#include "mosq_broker.h"
#include "esp_tls.h"
#include "mqtt_client.h"
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
  struct IdMapping {
    std::string device;
    std::string external;
    // hm2mqtt publishes/listens under an AES-derived variant of the Bluetooth
    // MAC on `marstek_energy` topics for B2500 device types. This is that
    // variant of `external`, precomputed at codegen time; empty when
    // `external` is not a MAC address.
    std::string external_encrypted;
  };

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_port(uint16_t port) { port_ = port; }
  void set_max_clients(uint16_t max_clients) { max_clients_ = max_clients; }
  void set_tls(bool tls) { tls_enabled_ = tls; }
  void set_tls_skip_verification(bool skip) { tls_skip_verification_ = skip; }

  void publish_message(const std::string &topic, const std::string &payload);
  void add_message_trigger(MosquittoMessageTrigger *trigger) { this->message_triggers_.push_back(trigger); }
  void set_publish_state(mqtt::MQTTClientState state) { this->publish_state_ = state; }

  /// True while the broker task is running. It goes false when mosq_broker_run()
  /// returns on its own, which is the failure behind the "communication stops
  /// after a few hours" reports: the relay stays reachable over WiFi while no
  /// battery message reaches the home broker any more.
  bool is_broker_running() const { return this->broker_started_ && !this->broker_exited_.load(); }
  /// True while the internal client that relays commands into the broker is
  /// connected. It can only connect once the broker itself is up, so a false
  /// here with a running broker means the broker is not accepting clients.
  bool is_publish_client_connected() const {
    return this->esp_mqtt_client_ != nullptr && this->publish_state_ == mqtt::MQTT_CLIENT_CONNECTED;
  }
  /// Messages the battery published on a `.../device/...` topic, i.e. the data
  /// the whole relay exists to move.
  uint32_t device_message_count() const { return this->device_messages_.load(); }
  /// Messages relayed towards the battery through publish_message().
  uint32_t app_message_count() const { return this->app_messages_.load(); }
  /// publish_message() calls that never reached the broker.
  uint32_t publish_error_count() const { return this->publish_errors_.load(); }
  /// How often the broker task had to be started again after exiting by itself.
  uint32_t broker_restart_count() const { return this->broker_restarts_; }
  /// Whether any `.../device/...` message has been seen since boot. Guards
  /// last_device_message(), which is meaningless before the first one.
  bool has_device_message() const { return this->has_device_message_.load(); }
  /// millis() when the last `.../device/...` message arrived.
  uint32_t last_device_message() const { return this->last_device_message_.load(); }
  void add_id_mapping(const std::string &device, const std::string &external,
                      const std::string &external_encrypted = "") {
    this->id_mappings_.push_back(IdMapping{device, external, external_encrypted});
  }

 protected:
  static void broker_task_(void *param);
  static void on_broker_message_callback(char *client, char *topic, char *data, int len, int qos, int retain);

  void handle_message_(char *topic, char *data, int len);
  void ensure_publish_client_();
  void teardown_publish_client_();
  std::string translate_external_to_device_(const std::string &topic) const;
  std::string translate_device_to_external_(const std::string &topic) const;

  uint16_t port_{1883};
  uint16_t max_clients_{20};
  TaskHandle_t broker_task_handle_{nullptr};
  struct mosq_broker_config broker_config_{};
  esp_tls_cfg_server_t tls_cfg_{};
  bool broker_started_{false};
  bool tls_enabled_{false};
  bool tls_skip_verification_{false};
  uint32_t broker_start_at_{0};
  /// How long to wait before (re)starting the broker task. Doubles on every
  /// restart and is reset once the broker accepts a client again.
  uint32_t broker_start_delay_{1000};
  uint32_t broker_restarts_{0};
  mqtt::MQTTClientState publish_state_{mqtt::MQTT_CLIENT_DISCONNECTED};
  uint32_t connect_begin_{0};
  esp_mqtt_client_handle_t esp_mqtt_client_{nullptr};
  std::vector<MosquittoMessageTrigger *> message_triggers_;
  std::vector<IdMapping> id_mappings_;

  // mosq_broker runs the broker, and calls its message callback, on its own
  // FreeRTOS task. These are written there and read from the ESPHome loop, so
  // they are atomic rather than plain counters.
  std::atomic<bool> broker_exited_{false};
  std::atomic<uint32_t> device_messages_{0};
  std::atomic<uint32_t> app_messages_{0};
  std::atomic<uint32_t> publish_errors_{0};
  std::atomic<uint32_t> last_device_message_{0};
  std::atomic<bool> has_device_message_{false};
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
