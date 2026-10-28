#include "mosquitto_broker.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esp_tls.h"
#include "mqtt_client.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace esphome {
namespace mosquitto_broker {

static const char *const TAG = "mosquitto_broker";
/// Client id of the relay's own client on the local broker. Messages it
/// publishes are commands from hm2mqtt, never the battery's own.
static const char *const PUBLISH_CLIENT_ID = "marsrelay-publish";
/// How long to wait before handing the battery its own poll back. A cloud
/// broker's round trip; the battery ignores anything that arrives sooner.
static const uint32_t SELF_POLL_RESEND_DELAY_MS = 1000;
static MosquittoBroker *global_broker = nullptr;
// How often loop() looks for batteries that stopped answering.
static const uint32_t STUCK_CHECK_INTERVAL_MS = 5000;
// A battery can drop off the access point just after a command reached the
// relay, before it answered; count such a drop as the one that silenced it.
static const uint32_t LEAVE_AFTER_COMMAND_SLACK_MS = 30000;
// Pauses per silence before recovery gives up on a battery.
static const uint8_t MAX_AP_PAUSES = 3;
// Departures remembered; a stuck battery leaves every few minutes.
static const size_t MAX_DEPARTURES = 32;
// What the access point is called while it is paused.
static const char *const PAUSED_SSID = "marsrelay-paused";

// Embedded self-signed certificate and key for TLS
// CN=mosquitto-broker, valid from 2026-01-21 to 2032-01-20
static const char server_cert_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDFzCCAf+gAwIBAgIUFwanTLrmHWXEKA6hwCvGbI/SpBQwDQYJKoZIhvcNAQEL\n"
    "BQAwGzEZMBcGA1UEAwwQbW9zcXVpdHRvLWJyb2tlcjAeFw0yNjAxMjEyMDQ0NDha\n"
    "Fw0zMjAxMjAyMDQ0NDhaMBsxGTAXBgNVBAMMEG1vc3F1aXR0by1icm9rZXIwggEi\n"
    "MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCVklzGn8kzKuXkBhkCxSJWfhxt\n"
    "OBbKsNV/+R9GS6RyZLFVZI0d+fqNulJG2yMGLWuMBvRDwTZ5iknb8Ij5fqOKHYWg\n"
    "9PvPv5a28zIWx+mJHNh/HrcWSTTbr2X2iRdnuRiVoLxXLAxfvhoh3T/NGr5JkM0b\n"
    "CEP8xUimbANo+jmzvOrdF26Z6s4z8l+PcaLp43dsS9wdEVnrX52WS1RfIUUcagNl\n"
    "4VP7VNao2PBaATJQByrIq0178CIepEU0QAThT+drTP4jUIk2Ru5JhlgQpqasSpSG\n"
    "ltD6UxAKxA1U5qrOgd5bY69Ta+30EvC1B5dc0fIqSs5QfKjWx6n6Mi15oPXtAgMB\n"
    "AAGjUzBRMB0GA1UdDgQWBBTotx1lsYm9ePPayNUIqEdnOxWcyDAfBgNVHSMEGDAW\n"
    "gBTotx1lsYm9ePPayNUIqEdnOxWcyDAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3\n"
    "DQEBCwUAA4IBAQBI7e8HPS0dh0B6hvdnJ6Fx9urTnqwq1ifrjytZGsHTCItYeh60\n"
    "NLsmMb38cXYlI7wHxmI+3mWIGEGJ6TVrv1XtzejvIhH8Jd/lKTs501LNty2Kkkm+\n"
    "q/wkunHl9dhz6D14V4a368Fea/WmOFy3mSnDWOrcBt3NDiNVkJ0W9BlJSFcQj+Na\n"
    "5H+cQSUquKnRkWrgPh71YAWnwrNfSVGEuQ0usjYbhRyb33oh2dknhZdg37VIIAcf\n"
    "tvLizQxg3GtAo8LGlGvxs1SXnAUxlqAEr6oR5MWJteSLgEyNeDtHplHnpLly25T7\n"
    "w1g0I+/fdB9qlOX2Io2CY2FLXC6yMOcXGeOi\n"
    "-----END CERTIFICATE-----\n";

static const char server_key_pem[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCVklzGn8kzKuXk\n"
    "BhkCxSJWfhxtOBbKsNV/+R9GS6RyZLFVZI0d+fqNulJG2yMGLWuMBvRDwTZ5iknb\n"
    "8Ij5fqOKHYWg9PvPv5a28zIWx+mJHNh/HrcWSTTbr2X2iRdnuRiVoLxXLAxfvhoh\n"
    "3T/NGr5JkM0bCEP8xUimbANo+jmzvOrdF26Z6s4z8l+PcaLp43dsS9wdEVnrX52W\n"
    "S1RfIUUcagNl4VP7VNao2PBaATJQByrIq0178CIepEU0QAThT+drTP4jUIk2Ru5J\n"
    "hlgQpqasSpSGltD6UxAKxA1U5qrOgd5bY69Ta+30EvC1B5dc0fIqSs5QfKjWx6n6\n"
    "Mi15oPXtAgMBAAECggEAKN7Zoxy+adDrCKfx5aPgginrspyE/dXcQR/dv+Ojh+6j\n"
    "1mWnee143YlwOhRfOaznmle8H1eIfyWekQ7lHufP4Em43gaTWG1Nzageo8L0uZa8\n"
    "QeAuv1Q5sV34SqmjT6Bwa1KEpH1Q1Eip8171tCH+pTOAGEbl7Qgrle4l8GWPpuEp\n"
    "hDurtVZKhcO8KdT5I4K06j/0xKcpnzoNQxQo09sazcxCbYVhx3/jhIgXsQEHL8lI\n"
    "VliaaUSUM7m/TMjc1YMduh/zSqioS2QrtrNmtV8raFwJoVZEC51EHtIOz6TgC4kE\n"
    "wh8fugooFOuQ4MuLc6LRc+r8iLvkml/Y40MWgbAMMQKBgQDIQFmVTk4mpwjdysRr\n"
    "DhTlHib/Z1XbHaxuRV0TyibuCCeHAGwJuA0gwqHGK5l2KM16LX/eQHejrAREuJSw\n"
    "6AsQ6hrGZnpmtlLOuZaf+RrZnoy+4yK7e2LkMd95EXdADY0hvxeUNZnOyeNqgbyv\n"
    "+JJCQFgcjaLNdyzcNI0VrTSB5wKBgQC/NiRC1lyruU9DBKVmKx6P1TGDYlz/4md4\n"
    "Uf8t4ixkj5zi1uiFJZRKR0HpYR1W7lLKZm9pfMk2c8xGJpTVK02Y9cO5NzRxAkNG\n"
    "WOI0R0P4Z1oyTzojw6aygfJHgIJZMXkqAqc566VgMWU/sNs5voFfa74yFGKdeJYg\n"
    "ICm6Wnt3CwKBgAt97eYsnT2InnCj/0upfjd72H8Vvg6aEFgvsNy+4CcO3r3Xn1ub\n"
    "bV1w7fnCbMckJk6Zp9noVzVUXNZYxWe1mVT6KlkyblnQosXsTqGVmR3eBHO78zVR\n"
    "KmawGgQHpZFOdcf3AHJn9RCx81QcZ+itWi9lI+lXk305FqD4fxQ8YWQHAoGAdLdT\n"
    "wwieYKQo4bvASnEfoqR8KLquEfPdPPCwVw2sE2YmWcDdBgk+T2jXruF8y0eGec21\n"
    "TCrDl91vX5LFXqmkIC5EXpZ4CFNdRV+UFF07/DD6OaNq1dHjuyre/Q5QgqlUUHR2\n"
    "J0DUHbeJGiuWZdUHm3tlCaSv3XdyDAIV9o6stqkCgYEAo+/MCN0Y9bUk2Pv/umNg\n"
    "1lrL8LFdQDDWbghhldtoG2sFYGXtNbHLw5BbwmABz+cQ6gWuaaDxUt41o2pZ6h96\n"
    "0DWHyXac/jFlCx1I94nBMArXFH/Eb58w0+vUvjDdLUHzDzuCnrJ7nyJb2mXxygRa\n"
    "Ekj7LjdQPex0u8iY/hda6sw=\n"
    "-----END PRIVATE KEY-----\n";

void MosquittoBroker::setup() {
  if (this->tls_enabled_) {
    ESP_LOGI(TAG, "Starting Mosquitto broker with TLS on port %u", this->port_);
  } else {
    ESP_LOGI(TAG, "Starting Mosquitto broker without TLS on port %u", this->port_);
  }
  static char broker_host[] = "0.0.0.0";
  this->broker_config_.host = broker_host;
  this->broker_config_.port = this->port_;
  // Note: max_clients_ is stored for logging/documentation purposes only.
  // The mosq_broker_config struct doesn't support max_clients configuration.
  // Connection limits are enforced by the system socket limit (CONFIG_LWIP_MAX_SOCKETS).
  // ESP32 has limited sockets (~10-16 default), and TLS connections use multiple sockets each.
  // With CONFIG_LWIP_MAX_SOCKETS=16, we can support: 1 listening socket + 1 publish client + ~3-5 external clients + system overhead
  
  // Configure TLS if enabled
  if (this->tls_enabled_) {
    // Configure TLS with embedded self-signed certificate
    // mbedtls expects PEM format: buffer must be null-terminated
    // For PEM strings, length should include null terminator (like binary embedding: end - start)
    this->tls_cfg_.servercert_buf = (const unsigned char *)server_cert_pem;
    this->tls_cfg_.servercert_bytes = strlen(server_cert_pem) + 1;  // Include null terminator
    this->tls_cfg_.serverkey_buf = (const unsigned char *)server_key_pem;
    this->tls_cfg_.serverkey_bytes = strlen(server_key_pem) + 1;  // Include null terminator
    this->broker_config_.tls_cfg = &this->tls_cfg_;
    ESP_LOGI(TAG, "TLS enabled with embedded self-signed certificate (cert: %zu bytes, key: %zu bytes)", 
             this->tls_cfg_.servercert_bytes, this->tls_cfg_.serverkey_bytes);
  } else {
    this->broker_config_.tls_cfg = nullptr;
  }
  
  this->broker_config_.handle_message_cb = &MosquittoBroker::on_broker_message_callback;

  // Access point joins and leaves, with the reason a client left: a battery
  // that drops off the access point and comes back can lose its MQTT session
  // for good, and this is where that shows up first.
  for (int32_t event : {WIFI_EVENT_AP_STACONNECTED, WIFI_EVENT_AP_STADISCONNECTED}) {
    if (esp_event_handler_register(WIFI_EVENT, event, &MosquittoBroker::ap_event_handler_, this) != ESP_OK) {
      ESP_LOGW(TAG, "Could not watch access point clients; join/leave logging and stuck-device recovery are off");
    }
  }

  global_broker = this;
  this->broker_start_at_ = esphome::millis();
}

void MosquittoBroker::loop() {
  // Refreshed first, so the paths below that return early still report. The
  // first round runs a few seconds after boot rather than a full interval in,
  // so the entities do not stay unknown that long.
  const uint32_t now = esphome::millis();

  // Evaluated every loop rather than on the diagnostics tick: an automation
  // should still fire when `diagnostics_interval` is long, or `never`.
  for (auto *trigger : this->liveness_triggers_) {
    trigger->update(this->has_device_message(), this->last_device_message());
  }

  if (this->diagnostics_interval_ms_ != SCHEDULER_DONT_RUN &&  // `never`
      now - this->last_diagnostics_ >= (this->diagnostics_started_ ? this->diagnostics_interval_ms_ : 5000)) {
    this->last_diagnostics_ = now;
    this->diagnostics_started_ = true;
    this->publish_diagnostics_();
  }

  this->resend_self_poll_();

  if (this->ap_paused_ && now - this->ap_paused_at_ >= this->pause_duration_ms_) {
    this->resume_ap_();
  }

  if (this->stuck_recovery_ && now - this->last_stuck_check_ >= STUCK_CHECK_INTERVAL_MS) {
    this->last_stuck_check_ = now;
    this->check_stuck_devices_();
  }

  if (this->broker_exited_.load()) {
    // mosq_broker_run() returned by itself: the broker is gone, but nothing
    // else notices -- WiFi stays up, the web server answers, and the battery's
    // messages go nowhere. Start it again instead of waiting for a power cycle.
    this->broker_exited_.store(false);
    this->broker_started_ = false;
    this->broker_restarts_++;
    // The publish client is holding a connection to a broker that no longer
    // exists; drop it so it is rebuilt against the new one.
    this->teardown_publish_client_();
    this->broker_start_delay_ = std::min<uint32_t>(this->broker_start_delay_ * 2, 60000);
    this->broker_start_at_ = esphome::millis();
    ESP_LOGE(TAG, "Broker task exited on its own; restarting it in %u ms (restart #%u)",
             (unsigned) this->broker_start_delay_, (unsigned) this->broker_restarts_);
    return;
  }

  if (!this->broker_started_ && esphome::millis() - this->broker_start_at_ > this->broker_start_delay_) {
    if (this->broker_task_handle_ == nullptr) {
      // 12 KiB stack: mbedTLS handshakes inside the broker task can use 8 KiB+ on
      // their own, so 4 KiB overflowed as soon as a TLS client connected.
      BaseType_t rc = xTaskCreate(&MosquittoBroker::broker_task_, "mosq_broker", 12288, this, 5,
                                  &this->broker_task_handle_);
      if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to create mosq_broker task (rc=%d), will retry", (int) rc);
        this->broker_task_handle_ = nullptr;
        this->broker_start_at_ = esphome::millis();  // back off ~1s before retrying
        return;
      }
    }
    this->broker_started_ = true;
    // Wait a bit longer for broker to be ready before connecting publish client
    this->connect_begin_ = esphome::millis() + 2000;  // Delay initial connection by 2 seconds
  }
  
  // Check connection status and reconnect if needed
  // Use longer retry interval to prevent socket exhaustion
  if (this->broker_started_) {
    if (this->publish_state_ == mqtt::MQTT_CLIENT_CONNECTED) {
      // The broker is up far enough to accept a client, so the next restart --
      // if there ever is one -- starts from the short delay again.
      this->broker_start_delay_ = 1000;
    }
    if (this->esp_mqtt_client_ == nullptr && esphome::millis() >= this->connect_begin_) {
      // Initial connection attempt or retry after delay
      this->ensure_publish_client_();
    } else if (this->esp_mqtt_client_ != nullptr && this->publish_state_ != mqtt::MQTT_CLIENT_CONNECTED) {
      uint32_t retry_interval = 30000;  // 30 seconds between retries
      if (esphome::millis() - this->connect_begin_ > retry_interval) {
        ESP_LOGW(TAG, "Publish client not connected after %lu ms, reconnecting...", 
                 esphome::millis() - this->connect_begin_);
        // Clean up failed connection before retrying
        this->teardown_publish_client_();
        this->connect_begin_ = esphome::millis();  // Reset retry timer
      }
    }
  }
}

void MosquittoBroker::publish_diagnostics_() {
#ifdef USE_BINARY_SENSOR
  if (this->running_binary_sensor_ != nullptr) {
    this->running_binary_sensor_->publish_state(this->is_broker_running());
  }
  if (this->publish_client_connected_binary_sensor_ != nullptr) {
    this->publish_client_connected_binary_sensor_->publish_state(this->is_publish_client_connected());
  }
  if (this->device_active_binary_sensor_ != nullptr) {
    // The battery publishes in bursts, so this is a timeout rather than a
    // per-message state.
    const bool active = this->has_device_message() &&
                        esphome::millis() - this->last_device_message() < this->device_active_timeout_ms_;
    this->device_active_binary_sensor_->publish_state(active);
  }
#endif

#ifdef USE_SENSOR
  if (this->device_messages_sensor_ != nullptr) {
    this->device_messages_sensor_->publish_state((float) this->device_message_count());
  }
  if (this->app_messages_sensor_ != nullptr) {
    this->app_messages_sensor_->publish_state((float) this->app_message_count());
  }
  if (this->publish_errors_sensor_ != nullptr) {
    this->publish_errors_sensor_->publish_state((float) this->publish_error_count());
  }
  if (this->broker_restarts_sensor_ != nullptr) {
    this->broker_restarts_sensor_->publish_state((float) this->broker_restart_count());
  }
  if (this->device_message_age_sensor_ != nullptr) {
    // NAN until the battery has published once: unknown, not zero.
    this->device_message_age_sensor_->publish_state(
        this->has_device_message() ? (esphome::millis() - this->last_device_message()) / 1000.0f : NAN);
  }
#endif
}

void MosquittoBroker::dump_config() {
  ESP_LOGCONFIG(TAG, "Mosquitto Broker:");
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  TLS: %s", this->tls_enabled_ ? "Enabled" : "Disabled");
  if (this->tls_enabled_) {
    ESP_LOGCONFIG(TAG, "  Certificate: Embedded self-signed");
    ESP_LOGCONFIG(TAG, "  Skip Verification: %s", this->tls_skip_verification_ ? "Yes" : "No");
  }
  ESP_LOGCONFIG(TAG, "  Max Clients: %u", this->max_clients_);
  ESP_LOGCONFIG(TAG, "  ID Mappings: %u", (unsigned) this->id_mappings_.size());
  if (this->stuck_recovery_) {
    ESP_LOGCONFIG(TAG, "  Stuck device recovery: after %u s unanswered, pause the AP for %u s",
                  (unsigned) (this->stuck_timeout_ms_ / 1000), (unsigned) (this->pause_duration_ms_ / 1000));
  }
}

void MosquittoBroker::publish_message(const std::string &topic, const std::string &payload) {
  if (!this->broker_started_) {
    ESP_LOGW(TAG, "Broker not started, skipping publish");
    this->publish_errors_.fetch_add(1);
    return;
  }
  if (this->publish_state_ != mqtt::MQTT_CLIENT_CONNECTED || this->esp_mqtt_client_ == nullptr) {
    this->ensure_publish_client_();
  }
  if (this->publish_state_ != mqtt::MQTT_CLIENT_CONNECTED || this->esp_mqtt_client_ == nullptr) {
    ESP_LOGW(TAG, "Publish client not connected, skipping publish");
    this->publish_errors_.fetch_add(1);
    return;
  }

  std::string translated = this->translate_external_to_device_(topic);
  if (translated != topic) {
    ESP_LOGV(TAG, "Translated external topic '%s' to device topic '%s'", topic.c_str(), translated.c_str());
  }

  if (this->publish_local_(translated, payload)) {
    this->note_command_(translated);
  }
}

bool MosquittoBroker::publish_local_(const std::string &topic, const std::string &payload) {
  int msg_id = esp_mqtt_client_publish(this->esp_mqtt_client_, topic.c_str(), payload.c_str(), payload.length(), 0, 0);
  if (msg_id < 0) {
    ESP_LOGW(TAG, "Publish failed for %s (error: %d)", topic.c_str(), msg_id);
    this->publish_errors_.fetch_add(1);
    return false;
  }
  this->app_messages_.fetch_add(1);
  return true;
}

void MosquittoBroker::resend_self_poll_() {
  std::string topic;
  std::string payload;
  {
    LockGuard guard(this->self_poll_lock_);
    if (!this->self_poll_pending_ || (int32_t) (esphome::millis() - this->self_poll_at_) < 0) {
      return;
    }
    this->self_poll_pending_ = false;
    topic = std::move(this->self_poll_topic_);
    payload = std::move(this->self_poll_payload_);
  }
  if (!this->is_publish_client_connected()) {
    ESP_LOGW(TAG, "Publish client not connected, cannot resend the battery's poll");
    this->publish_errors_.fetch_add(1);
    return;
  }
  // Already a device-side topic: no id translation.
  ESP_LOGD(TAG, "Resending the battery's poll on '%s'", topic.c_str());
  this->publish_local_(topic, payload);
}

namespace {

// Device families whose hm2mqtt integration derives an encrypted topic id from
// the Bluetooth MAC on `marstek_energy` topics. This list must mirror
// hm2mqtt's `encryptedDeviceTypes` exactly. HMB is deliberately absent even
// though it belongs to the B2500 family: hm2mqtt uses the plain MAC for HMB,
// and HMB firmware never uses the `marstek_energy` prefix anyway.
bool is_b2500_type_segment(const std::string &segment) {
  static const char *const B2500_BASE_TYPES[] = {"HMA", "HMF", "HMK", "HMJ"};
  std::string base = segment.substr(0, segment.find('-'));
  for (auto &c : base) {
    c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
  }
  for (const char *base_type : B2500_BASE_TYPES) {
    if (base == base_type) {
      return true;
    }
  }
  return false;
}

// True when the external side of this topic uses the encrypted MAC variant:
// hm2mqtt only does that on `marstek_energy/<b2500-type>/...` topics.
bool topic_uses_encrypted_external(const std::string &topic) {
  size_t first = topic.find('/');
  if (first == std::string::npos) {
    return false;
  }
  if (topic.compare(0, first, "marstek_energy") != 0) {
    return false;
  }
  size_t second = topic.find('/', first + 1);
  size_t type_end = (second == std::string::npos) ? topic.size() : second;
  return is_b2500_type_segment(topic.substr(first + 1, type_end - first - 1));
}

}  // namespace

std::string MosquittoBroker::translate_external_to_device_(const std::string &topic) const {
  if (this->id_mappings_.empty()) {
    return topic;
  }
  std::string result;
  result.reserve(topic.size());
  size_t pos = 0;
  while (pos <= topic.size()) {
    size_t next = topic.find('/', pos);
    size_t end = (next == std::string::npos) ? topic.size() : next;
    bool replaced = false;
    for (const auto &mapping : this->id_mappings_) {
      // Accept both external forms regardless of prefix, so already-translated
      // or manually configured encrypted ids keep working.
      const bool matches_plain = end - pos == mapping.external.size() &&
                                 topic.compare(pos, mapping.external.size(), mapping.external) == 0;
      const bool matches_encrypted = !mapping.external_encrypted.empty() &&
                                     end - pos == mapping.external_encrypted.size() &&
                                     topic.compare(pos, mapping.external_encrypted.size(),
                                                   mapping.external_encrypted) == 0;
      if (matches_plain || matches_encrypted) {
        result.append(mapping.device);
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      result.append(topic, pos, end - pos);
    }
    if (next == std::string::npos) {
      break;
    }
    result.push_back('/');
    pos = next + 1;
  }
  return result;
}

std::string MosquittoBroker::translate_device_to_external_(const std::string &topic) const {
  if (this->id_mappings_.empty()) {
    return topic;
  }
  const bool use_encrypted = topic_uses_encrypted_external(topic);
  std::string result;
  result.reserve(topic.size());
  size_t pos = 0;
  while (pos <= topic.size()) {
    size_t next = topic.find('/', pos);
    size_t end = (next == std::string::npos) ? topic.size() : next;
    bool replaced = false;
    for (const auto &mapping : this->id_mappings_) {
      const std::string &device = mapping.device;
      if (end - pos == device.size() && topic.compare(pos, device.size(), device) == 0) {
        if (use_encrypted && !mapping.external_encrypted.empty()) {
          result.append(mapping.external_encrypted);
        } else {
          result.append(mapping.external);
        }
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      result.append(topic, pos, end - pos);
    }
    if (next == std::string::npos) {
      break;
    }
    result.push_back('/');
    pos = next + 1;
  }
  return result;
}

void MosquittoBroker::broker_task_(void *param) {
  auto *self = static_cast<MosquittoBroker *>(param);
  if (self == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  mosq_broker_run(&self->broker_config_);
  // Only reached when the broker gave up. Clear the handle and say so, so
  // loop() can create a new task rather than trusting a dead one.
  self->broker_task_handle_ = nullptr;
  self->broker_exited_.store(true);
  vTaskDelete(nullptr);
}

void MosquittoBroker::on_broker_message_callback(char *client, char *topic, char *data, int len, int qos, int retain) {
  (void) qos;
  (void) retain;
  if (topic == nullptr || data == nullptr) {
    return;
  }
  if (global_broker != nullptr) {
    global_broker->handle_message_(client != nullptr ? client : "", topic, data, len);
  }
}

void MosquittoBroker::handle_message_(const char *client, char *topic, char *data, int len) {
  std::string topic_str(topic);
  std::string payload(data, len);

  // `.../device/...` is what the battery itself publishes; everything else on
  // the local broker is a command on its way in. Counting only the former
  // makes this a liveness signal for the battery rather than for the relay's
  // own traffic.
  if (topic_str.find("/device/") != std::string::npos) {
    this->device_messages_.fetch_add(1);
    this->last_device_message_.store(esphome::millis());
    this->has_device_message_.store(true);
    this->note_device_message_(topic_str);
  }

  // Right after connecting, the battery polls itself: it publishes `cd=01` to
  // its own `.../App/<id>/ctrl` and expects the broker to deliver it back, upon
  // which it answers on `.../device/...`. But it only starts reading incoming
  // messages once that publish has completed, and a broker on the same access
  // point echoes it before then -- so the battery drops its own poll and stays
  // silent until something else polls it. Hand the poll back once it listens.
  if (strcmp(client, PUBLISH_CLIENT_ID) != 0 && topic_str.find("/App/") != std::string::npos &&
      (payload == "cd=01" || payload == "cd=1")) {
    ESP_LOGI(TAG, "Battery connected on '%s'; resending its own poll in %u ms", topic_str.c_str(),
             (unsigned) SELF_POLL_RESEND_DELAY_MS);
    LockGuard guard(this->self_poll_lock_);
    this->self_poll_topic_ = topic_str;
    this->self_poll_payload_ = payload;
    this->self_poll_at_ = esphome::millis() + SELF_POLL_RESEND_DELAY_MS;
    this->self_poll_pending_ = true;
  }

  std::string translated = this->translate_device_to_external_(topic_str);
  if (translated != topic_str) {
    ESP_LOGV(TAG, "Translated device topic '%s' to external topic '%s'", topic_str.c_str(), translated.c_str());
  }

  for (auto *trigger : this->message_triggers_) {
    trigger->trigger(translated, payload);
  }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  auto *self = static_cast<MosquittoBroker *>(handler_args);
  if (self == nullptr) {
    return;
  }
  
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  (void)event;
  
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(TAG, "Publish client connected");
      self->set_publish_state(mqtt::MQTT_CLIENT_CONNECTED);
      break;
    case MQTT_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "Publish client disconnected");
      self->set_publish_state(mqtt::MQTT_CLIENT_DISCONNECTED);
      break;
    case MQTT_EVENT_ERROR:
      ESP_LOGW(TAG, "Publish client error");
      self->set_publish_state(mqtt::MQTT_CLIENT_DISCONNECTED);
      break;
    default:
      break;
  }
}

void MosquittoBroker::teardown_publish_client_() {
  if (this->esp_mqtt_client_ != nullptr) {
    esp_mqtt_client_stop(this->esp_mqtt_client_);
    esp_mqtt_client_destroy(this->esp_mqtt_client_);
    this->esp_mqtt_client_ = nullptr;
  }
  this->publish_state_ = mqtt::MQTT_CLIENT_DISCONNECTED;
}

void MosquittoBroker::ensure_publish_client_() {
  if (!this->broker_started_) {
    return;
  }
  if (this->publish_state_ == mqtt::MQTT_CLIENT_CONNECTING) {
    return;
  }
  
  // Disconnect existing client if any
  this->teardown_publish_client_();
  
  // Configure ESP-IDF MQTT client for TLS
  esp_mqtt_client_config_t mqtt_cfg = {};
  mqtt_cfg.broker.address.hostname = "127.0.0.1";
  mqtt_cfg.broker.address.port = this->port_;
  
  if (this->tls_enabled_) {
    mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
    // Always provide a certificate to satisfy ESP-IDF's requirement
    // When tls_skip_verification is true, we skip CN check which allows
    // connection even if the certificate doesn't match perfectly
    mqtt_cfg.broker.verification.use_global_ca_store = false;  // Explicitly disable global CA store
    mqtt_cfg.broker.verification.certificate = server_cert_pem;
    mqtt_cfg.broker.verification.certificate_len = strlen(server_cert_pem) + 1;  // Include null terminator
    mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
    mqtt_cfg.broker.verification.common_name = nullptr;
    
    if (this->tls_skip_verification_) {
      ESP_LOGW(TAG, "TLS client: certificate verification relaxed (CN check disabled, accepts broker cert)");
    } else {
      ESP_LOGI(TAG, "TLS client: using broker certificate with CN check disabled");
    }
  } else {
    mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  }
  
  mqtt_cfg.credentials.client_id = PUBLISH_CLIENT_ID;
  mqtt_cfg.session.keepalive = 60;
  mqtt_cfg.session.disable_clean_session = false;
  
  this->esp_mqtt_client_ = esp_mqtt_client_init(&mqtt_cfg);
  if (this->esp_mqtt_client_ == nullptr) {
    ESP_LOGE(TAG, "Failed to initialize MQTT client");
    return;
  }
  
  esp_mqtt_client_register_event(this->esp_mqtt_client_, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), mqtt_event_handler, this);
  esp_err_t err = esp_mqtt_client_start(this->esp_mqtt_client_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start MQTT client: %d", err);
    esp_mqtt_client_destroy(this->esp_mqtt_client_);
    this->esp_mqtt_client_ = nullptr;
    return;
  }
  
  this->publish_state_ = mqtt::MQTT_CLIENT_CONNECTING;
  this->connect_begin_ = esphome::millis();
  ESP_LOGI(TAG, "Connecting publish client to broker on port %u", this->port_);
}

namespace {

// `<prefix>/<type>/<kind>/<id>/...` -> `<id>`, where kind is "device" or "App".
std::string device_id_in(const std::string &topic, const char *kind) {
  const std::string marker = std::string("/") + kind + "/";
  const size_t pos = topic.find(marker);
  if (pos == std::string::npos) {
    return "";
  }
  const size_t start = pos + marker.size();
  const size_t end = topic.find('/', start);
  return topic.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

uint64_t mac_key(const uint8_t *mac) {
  uint64_t key = 0;
  for (int i = 0; i < 6; i++) {
    key = (key << 8) | mac[i];
  }
  return key;
}

std::string mac_str(uint64_t key) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", (unsigned) (key >> 40) & 0xff,
           (unsigned) (key >> 32) & 0xff, (unsigned) (key >> 24) & 0xff, (unsigned) (key >> 16) & 0xff,
           (unsigned) (key >> 8) & 0xff, (unsigned) key & 0xff);
  return buf;
}

// The 802.11 and ESP-IDF reasons a client can leave with that say something
// about who ended the association, and why.
const char *leave_reason_name(uint16_t reason) {
  switch (reason) {
    case WIFI_REASON_UNSPECIFIED:
      return "unspecified";
    case WIFI_REASON_AUTH_EXPIRE:
      return "auth expired";
    case WIFI_REASON_AUTH_LEAVE:
      return "client left (deauth)";
    case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY:
      return "inactivity";
    case WIFI_REASON_ASSOC_LEAVE:
      return "client left (disassoc)";
    case WIFI_REASON_CLASS2_FRAME_FROM_NONAUTH_STA:
      return "frame from unauthenticated client";
    case WIFI_REASON_CLASS3_FRAME_FROM_NONASSOC_STA:
      return "frame from unassociated client";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "4-way handshake timeout";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
      return "group key update timeout";
    case WIFI_REASON_BEACON_TIMEOUT:
      return "beacon timeout";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "handshake timeout";
    case WIFI_REASON_SA_QUERY_TIMEOUT:
      return "SA query timeout";
    default:
      return "other";
  }
}

}  // namespace

void MosquittoBroker::ap_event_handler_(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
  (void) base;
  auto *self = static_cast<MosquittoBroker *>(arg);
  if (event_id == WIFI_EVENT_AP_STACONNECTED) {
    const auto *event = static_cast<wifi_event_ap_staconnected_t *>(event_data);
    self->on_station_joined_(event->mac, event->aid);
  } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    const auto *event = static_cast<wifi_event_ap_stadisconnected_t *>(event_data);
    self->on_station_left_(event->mac, event->reason);
  }
}

// Runs on the default event loop task.
void MosquittoBroker::on_station_joined_(const uint8_t *mac, uint16_t aid) {
  (void) aid;
  const uint64_t key = mac_key(mac);
  {
    LockGuard guard(this->stations_lock_);
    if (std::find(this->connected_.begin(), this->connected_.end(), key) == this->connected_.end()) {
      this->connected_.push_back(key);
    }
  }
  ESP_LOGD(TAG, "AP client %s joined", mac_str(key).c_str());
}

// Runs on the default event loop task.
void MosquittoBroker::on_station_left_(const uint8_t *mac, uint16_t reason) {
  const uint64_t key = mac_key(mac);
  {
    LockGuard guard(this->stations_lock_);
    this->connected_.erase(std::remove(this->connected_.begin(), this->connected_.end(), key), this->connected_.end());
    if (this->departures_.size() >= MAX_DEPARTURES) {
      this->departures_.erase(this->departures_.begin());
    }
    this->departures_.push_back(Departure{key, esphome::millis()});
  }
  ESP_LOGD(TAG, "AP client %s left (reason %u: %s)", mac_str(key).c_str(), reason, leave_reason_name(reason));
}

// Runs on the broker task.
void MosquittoBroker::note_device_message_(const std::string &topic) {
  const std::string id = device_id_in(topic, "device");
  if (id.empty()) {
    return;
  }
  LockGuard guard(this->devices_lock_);
  DeviceState &device = this->devices_[id];
  device.last_message = esphome::millis();
  device.unanswered = false;
  device.checked = false;
  device.suspects.clear();
  device.pauses = 0;
  device.gave_up = false;
}

// Runs on the loop task, for every command relayed towards a battery.
void MosquittoBroker::note_command_(const std::string &topic) {
  const std::string id = device_id_in(topic, "App");
  if (id.empty()) {
    return;
  }
  LockGuard guard(this->devices_lock_);
  auto it = this->devices_.find(id);
  // Every relay receives every command, including those for batteries on
  // another relay. Only a device that has published here before counts.
  if (it == this->devices_.end() || it->second.unanswered) {
    return;
  }
  it->second.unanswered = true;
  it->second.unanswered_since = esphome::millis();
}

// A battery can drop off the access point for a few seconds and come back
// with its MQTT session gone. Its WiFi module reconnects by itself and does
// not tell the battery, so the battery never rebuilds the session and stays
// silent until it is reset. What the module does report is not finding the
// access point at all, and the battery then starts over. So once commands
// relayed to it have gone unanswered for `stuck_timeout_ms_`, and a client
// that dropped off the access point between its last message and the first
// command it missed is back on it, take the access point away for
// `pause_duration_ms_`. Every client on it drops for that long. Nothing happens
// for a battery nobody polls, or when no client dropped in that window. If the
// battery is still silent a timeout after the pause, it is paused again, up to
// MAX_AP_PAUSES times.
void MosquittoBroker::check_stuck_devices_() {
  if (this->ap_paused_) {
    return;
  }
  const uint32_t now = esphome::millis();
  struct Stuck {
    std::string id;
    uint32_t last_message;
    uint32_t unanswered_since;
    std::vector<uint64_t> suspects;
    uint8_t pauses;
  };
  std::vector<Stuck> stuck;
  {
    LockGuard guard(this->devices_lock_);
    for (auto &kv : this->devices_) {
      DeviceState &device = kv.second;
      if (!device.unanswered || device.gave_up || now - device.unanswered_since < this->stuck_timeout_ms_) {
        continue;
      }
      if (device.checked && (int32_t) (now - device.next_check) < 0) {
        continue;
      }
      device.checked = true;
      device.next_check = now + this->stuck_timeout_ms_;
      stuck.push_back(Stuck{kv.first, device.last_message, device.unanswered_since, device.suspects, device.pauses});
    }
  }
  if (stuck.empty()) {
    return;
  }

  bool pause = false;
  for (auto &device : stuck) {
    bool back = false;
    {
      LockGuard guard(this->stations_lock_);
      if (device.suspects.empty()) {
        for (const auto &departure : this->departures_) {
          if ((int32_t) (departure.at - device.last_message) > 0 &&
              (int32_t) (departure.at - (device.unanswered_since + LEAVE_AFTER_COMMAND_SLACK_MS)) <= 0 &&
              std::find(device.suspects.begin(), device.suspects.end(), departure.mac) == device.suspects.end()) {
            device.suspects.push_back(departure.mac);
          }
        }
      }
      for (uint64_t mac : device.suspects) {
        if (std::find(this->connected_.begin(), this->connected_.end(), mac) != this->connected_.end()) {
          back = true;
        }
      }
    }
    const unsigned silent_s = (unsigned) ((now - device.last_message) / 1000);
    bool answered = false;
    bool give_up = device.pauses >= MAX_AP_PAUSES;
    {
      LockGuard guard(this->devices_lock_);
      auto it = this->devices_.find(device.id);
      // Unless it answered in the meantime, which resets all of this.
      if (it == this->devices_.end() || !it->second.unanswered) {
        answered = true;
      } else {
        it->second.suspects = device.suspects;
        if (back && give_up) {
          it->second.gave_up = true;
        } else if (back) {
          it->second.pauses++;
          // Another look once the pause is over and the battery had a timeout
          // to answer.
          it->second.next_check = now + this->pause_duration_ms_ + this->stuck_timeout_ms_;
        }
      }
    }
    if (answered) {
      continue;
    }
    if (device.suspects.empty()) {
      ESP_LOGD(TAG, "Device %s has not answered for %u s, but no AP client dropped off between its last message "
               "and the first command it missed; leaving it alone", device.id.c_str(), silent_s);
      continue;
    }
    std::string macs;
    for (uint64_t mac : device.suspects) {
      macs += (macs.empty() ? "" : ", ") + mac_str(mac);
    }
    if (!back) {
      ESP_LOGD(TAG, "Device %s has not answered for %u s after AP client %s dropped off, which has not come back; "
               "leaving it alone", device.id.c_str(), silent_s, macs.c_str());
      continue;
    }
    if (give_up) {
      ESP_LOGW(TAG, "Device %s is still not answering after %u access point pauses; giving up until it publishes "
               "again. Resetting the battery brings it back.", device.id.c_str(), (unsigned) MAX_AP_PAUSES);
      continue;
    }
    ESP_LOGW(TAG, "Device %s stopped answering after AP client %s dropped off the AP and came back (pause %u of %u)",
             device.id.c_str(), macs.c_str(), (unsigned) device.pauses + 1, (unsigned) MAX_AP_PAUSES);
    pause = true;
  }
  if (pause) {
    this->pause_ap_();
  }
}

// Swaps the access point for a hidden one nobody can join, so a battery that
// rescans for the real one does not find it. The real settings come back in
// resume_ap_().
bool MosquittoBroker::pause_ap_() {
  wifi_config_t conf{};
  if (esp_wifi_get_config(WIFI_IF_AP, &conf) != ESP_OK || conf.ap.ssid_len == 0) {
    ESP_LOGW(TAG, "Could not read the access point settings; not pausing it");
    return false;
  }
  this->saved_ap_config_ = conf;

  wifi_config_t paused = conf;
  memset(paused.ap.ssid, 0, sizeof(paused.ap.ssid));
  memcpy(paused.ap.ssid, PAUSED_SSID, strlen(PAUSED_SSID));
  paused.ap.ssid_len = strlen(PAUSED_SSID);
  paused.ap.ssid_hidden = 1;
  // A password nobody knows, so nothing joins in the meantime.
  static const char ALPHABET[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  memset(paused.ap.password, 0, sizeof(paused.ap.password));
  for (size_t i = 0; i < 24; i++) {
    paused.ap.password[i] = ALPHABET[esp_random() % (sizeof(ALPHABET) - 1)];
  }
  paused.ap.authmode = WIFI_AUTH_WPA2_PSK;
  esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &paused);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Could not pause the access point: %s", esp_err_to_name(err));
    return false;
  }
  this->ap_paused_ = true;
  this->ap_paused_at_ = esphome::millis();
  ESP_LOGW(TAG, "Access point paused for %u s; every client on it is off until then",
           (unsigned) (this->pause_duration_ms_ / 1000));
  return true;
}

void MosquittoBroker::resume_ap_() {
  this->ap_paused_ = false;
  wifi_config_t current{};
  // If the wifi component set the access point up again in the meantime, its
  // settings are the real ones already.
  if (esp_wifi_get_config(WIFI_IF_AP, &current) == ESP_OK &&
      (current.ap.ssid_len != strlen(PAUSED_SSID) || memcmp(current.ap.ssid, PAUSED_SSID, current.ap.ssid_len) != 0)) {
    ESP_LOGI(TAG, "Access point is back already");
    return;
  }
  esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &this->saved_ap_config_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Could not restore the access point: %s", esp_err_to_name(err));
    return;
  }
  ESP_LOGI(TAG, "Access point resumed");
}

}  // namespace mosquitto_broker
}  // namespace esphome
