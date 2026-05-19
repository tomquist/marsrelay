#include "mosquitto_broker.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esp_tls.h"
#include "mqtt_client.h"
#include "esp_event.h"
#include <cstring>

namespace esphome {
namespace mosquitto_broker {

static const char *const TAG = "mosquitto_broker";
static MosquittoBroker *global_broker = nullptr;

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

  global_broker = this;
  this->broker_start_at_ = esphome::millis();
}

void MosquittoBroker::loop() {
  if (!this->broker_started_ && esphome::millis() - this->broker_start_at_ > 1000) {
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
    if (this->esp_mqtt_client_ == nullptr && esphome::millis() >= this->connect_begin_) {
      // Initial connection attempt or retry after delay
      this->ensure_publish_client_();
    } else if (this->esp_mqtt_client_ != nullptr && this->publish_state_ != mqtt::MQTT_CLIENT_CONNECTED) {
      uint32_t retry_interval = 30000;  // 30 seconds between retries
      if (esphome::millis() - this->connect_begin_ > retry_interval) {
        ESP_LOGW(TAG, "Publish client not connected after %lu ms, reconnecting...", 
                 esphome::millis() - this->connect_begin_);
        // Clean up failed connection before retrying
        esp_mqtt_client_stop(this->esp_mqtt_client_);
        esp_mqtt_client_destroy(this->esp_mqtt_client_);
        this->esp_mqtt_client_ = nullptr;
        this->publish_state_ = mqtt::MQTT_CLIENT_DISCONNECTED;
        this->connect_begin_ = esphome::millis();  // Reset retry timer
      }
    }
  }
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
}

void MosquittoBroker::publish_message(const std::string &topic, const std::string &payload) {
  if (!this->broker_started_) {
    ESP_LOGW(TAG, "Broker not started, skipping publish");
    return;
  }
  if (this->publish_state_ != mqtt::MQTT_CLIENT_CONNECTED || this->esp_mqtt_client_ == nullptr) {
    this->ensure_publish_client_();
  }
  if (this->publish_state_ != mqtt::MQTT_CLIENT_CONNECTED || this->esp_mqtt_client_ == nullptr) {
    ESP_LOGW(TAG, "Publish client not connected, skipping publish");
    return;
  }

  std::string translated = this->translate_external_to_device_(topic);
  if (translated != topic) {
    ESP_LOGV(TAG, "Translated external topic '%s' to device topic '%s'", topic.c_str(), translated.c_str());
  }

  int msg_id = esp_mqtt_client_publish(this->esp_mqtt_client_, translated.c_str(), payload.c_str(), payload.length(), 0, 0);
  if (msg_id < 0) {
    ESP_LOGW(TAG, "Publish failed for %s (error: %d)", translated.c_str(), msg_id);
  }
}

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
      const std::string &external = mapping.second;
      if (end - pos == external.size() && topic.compare(pos, external.size(), external) == 0) {
        result.append(mapping.first);
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
  std::string result;
  result.reserve(topic.size());
  size_t pos = 0;
  while (pos <= topic.size()) {
    size_t next = topic.find('/', pos);
    size_t end = (next == std::string::npos) ? topic.size() : next;
    bool replaced = false;
    for (const auto &mapping : this->id_mappings_) {
      const std::string &device = mapping.first;
      if (end - pos == device.size() && topic.compare(pos, device.size(), device) == 0) {
        result.append(mapping.second);
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

void MosquittoBroker::ensure_publish_client_() {
  if (!this->broker_started_) {
    return;
  }
  if (this->publish_state_ == mqtt::MQTT_CLIENT_CONNECTING) {
    return;
  }
  
  // Disconnect existing client if any
  if (this->esp_mqtt_client_ != nullptr) {
    esp_mqtt_client_stop(this->esp_mqtt_client_);
    esp_mqtt_client_destroy(this->esp_mqtt_client_);
    this->esp_mqtt_client_ = nullptr;
  }
  
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
  
  mqtt_cfg.credentials.client_id = "marsrelay-publish";
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

}  // namespace mosquitto_broker
}  // namespace esphome
