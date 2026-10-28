#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/mqtt/mqtt_backend_esp32.h"
#include "esphome/components/mqtt/mqtt_client.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

extern "C" {
#include "mosq_broker.h"
#include "esp_tls.h"
#include "mqtt_client.h"
}

#include "esp_event.h"
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

/// Fires when the battery stops publishing for longer than `timeout_ms`, or
/// when it starts again after that. Each automation carries its own timeout and
/// its own state, so a config can act at several thresholds.
///
/// No timeout fires before the battery has published once: a relay that has
/// just booted has not lost anything yet. A recovery fires only after a timeout
/// did, so the first message of the day is not a recovery either.
class DeviceLivenessTrigger : public Trigger<> {
 public:
  DeviceLivenessTrigger(uint32_t timeout_ms, bool fire_on_timeout)
      : timeout_ms_(timeout_ms), fire_on_timeout_(fire_on_timeout) {}

  void update(bool seen, uint32_t last_ms) {
    const bool active = seen && esphome::millis() - last_ms < this->timeout_ms_;
    switch (this->state_) {
      case WAITING:
        if (active) {
          this->state_ = ACTIVE;
        }
        break;
      case ACTIVE:
        if (!active) {
          this->state_ = TIMED_OUT;
          if (this->fire_on_timeout_) {
            this->trigger();
          }
        }
        break;
      case TIMED_OUT:
        if (active) {
          this->state_ = ACTIVE;
          if (!this->fire_on_timeout_) {
            this->trigger();
          }
        }
        break;
    }
  }

 protected:
  enum State : uint8_t { WAITING, ACTIVE, TIMED_OUT };
  uint32_t timeout_ms_;
  bool fire_on_timeout_;
  State state_{WAITING};
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
  void add_liveness_trigger(DeviceLivenessTrigger *trigger) { this->liveness_triggers_.push_back(trigger); }
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
  /// Messages relayed towards the battery: commands through publish_message(),
  /// and the battery's own poll handed back to it.
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

  /// How often the diagnostic entities below are refreshed.
  void set_diagnostics_interval(uint32_t interval_ms) { this->diagnostics_interval_ms_ = interval_ms; }

  /// Recover a battery that dropped off the access point, came back, and
  /// stopped answering. After `timeout_ms` of relayed commands going
  /// unanswered, the clients that rejoined the access point since its last
  /// message are kept off it for `duration_ms`. See check_stuck_devices_().
  void set_stuck_recovery(uint32_t timeout_ms, uint32_t duration_ms) {
    this->stuck_recovery_ = true;
    this->stuck_timeout_ms_ = timeout_ms;
    this->kick_duration_ms_ = duration_ms;
  }

#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(running)
  SUB_BINARY_SENSOR(device_active)
  SUB_BINARY_SENSOR(publish_client_connected)
  /// How long the battery may stay silent before `device_active` goes off.
  void set_device_active_timeout(uint32_t timeout_ms) { this->device_active_timeout_ms_ = timeout_ms; }
#endif

#ifdef USE_SENSOR
  SUB_SENSOR(device_messages)
  SUB_SENSOR(app_messages)
  SUB_SENSOR(publish_errors)
  SUB_SENSOR(broker_restarts)
  SUB_SENSOR(device_message_age)
#endif

 protected:
  static void broker_task_(void *param);
  static void on_broker_message_callback(char *client, char *topic, char *data, int len, int qos, int retain);

  void handle_message_(const char *client, char *topic, char *data, int len);
  bool publish_local_(const std::string &topic, const std::string &payload);
  void resend_self_poll_();
  void ensure_publish_client_();
  void teardown_publish_client_();
  void publish_diagnostics_();
  std::string translate_external_to_device_(const std::string &topic) const;
  std::string translate_device_to_external_(const std::string &topic) const;

  static void ap_event_handler_(void *arg, esp_event_base_t base, int32_t event_id, void *event_data);
  void on_station_joined_(const uint8_t *mac, uint16_t aid);
  void on_station_left_(const uint8_t *mac, uint16_t reason);
  void note_device_message_(const std::string &topic);
  void note_command_(const std::string &topic);
  void check_stuck_devices_();

  /// What the broker knows about one battery, keyed by its device-side id.
  struct DeviceState {
    uint32_t last_message{0};
    /// A command was relayed after the last message and is still unanswered.
    bool unanswered{false};
    uint32_t unanswered_since{0};
    /// When recovery last acted on it, so it does not act again every loop.
    bool recovery_attempted{false};
    uint32_t last_recovery{0};
    /// The clients whose drop silenced it, found on the first attempt and
    /// kept for the retries: a later drop, the client's own or one caused by
    /// recovery, must not replace it.
    std::vector<uint64_t> suspects;
    /// Kicks so far; each retry doubles the time a suspect is kept off.
    uint8_t kicks{0};
    /// How long the last kick kept the suspects off. The next attempt waits
    /// for that outage to end, plus a full timeout for the device to answer.
    uint32_t last_kick_duration{0};
  };
  /// A client of the access point, keyed by MAC address.
  struct Station {
    uint16_t aid{0};
    bool connected{false};
    uint32_t last_join{0};
    bool has_left{false};
    uint32_t last_leave{0};
    /// Turned away on every rejoin until `blocked_until`.
    bool blocked{false};
    uint32_t blocked_until{0};
    /// Whether it has left since it was blocked; only the first departure is
    /// logged at DEBUG.
    bool left_while_blocked{false};
  };

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
  uint32_t diagnostics_interval_ms_{60000};
  uint32_t last_diagnostics_{0};
  bool diagnostics_started_{false};
#ifdef USE_BINARY_SENSOR
  uint32_t device_active_timeout_ms_{900000};
#endif
  mqtt::MQTTClientState publish_state_{mqtt::MQTT_CLIENT_DISCONNECTED};
  uint32_t connect_begin_{0};
  esp_mqtt_client_handle_t esp_mqtt_client_{nullptr};
  std::vector<MosquittoMessageTrigger *> message_triggers_;
  std::vector<DeviceLivenessTrigger *> liveness_triggers_;
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

  // Written from the broker task (device messages), the loop (commands) and the
  // default event loop task (access point events); hence the locks.
  Mutex devices_lock_;
  std::map<std::string, DeviceState> devices_;
  Mutex stations_lock_;
  std::map<uint64_t, Station> stations_;
  bool stuck_recovery_{false};
  uint32_t stuck_timeout_ms_{300000};
  uint32_t kick_duration_ms_{60000};
  uint32_t last_stuck_check_{0};

  // The battery's own poll, queued by the broker task for loop() to send back
  // once the battery is ready to read it. See handle_message_().
  Mutex self_poll_lock_;
  std::string self_poll_topic_;
  std::string self_poll_payload_;
  uint32_t self_poll_at_{0};
  bool self_poll_pending_{false};
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
