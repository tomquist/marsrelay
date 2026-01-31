#include "shelly_emulator.h"

#ifdef USE_SHELLY_EMULATOR

#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <ArduinoJson.h>
#include <lwip/sockets.h>
#include <cstring>

namespace esphome {
namespace shelly_emulator {

static const char *const TAG = "shelly_emulator";

void ShellyEmulator::setup() { this->start_(); }

float ShellyEmulator::get_setup_priority() const {
  // After WiFi
  return setup_priority::WIFI - 1.0f;
}

void ShellyEmulator::dump_config() {
  ESP_LOGCONFIG(TAG, "Shelly UDP Emulator:");
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  Device ID: %s", this->device_id_.c_str());
  ESP_LOGCONFIG(TAG, "  Power sensors: %u", (unsigned) this->power_sensors_.size());
  ESP_LOGCONFIG(TAG, "  Active: %s", this->active_ ? "Yes" : "No");
}

void ShellyEmulator::start_() {
  if (this->active_) {
    ESP_LOGW(TAG, "Shelly emulator already active");
    return;
  }

  if (this->port_ == 0) {
    ESP_LOGE(TAG, "No port configured for Shelly emulator");
    return;
  }

  this->socket_ = socket::socket_ip_loop_monitored(SOCK_DGRAM, IPPROTO_UDP);
  if (this->socket_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create UDP socket");
    return;
  }

  int enable = 1;
  this->socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  // Bind to all interfaces on the target port
  struct sockaddr_storage addr = {};
  socklen_t addr_len = socket::set_sockaddr_any((struct sockaddr *) &addr, sizeof(addr), this->port_);

  int err = this->socket_->bind((struct sockaddr *) &addr, addr_len);
  if (err != 0) {
    ESP_LOGE(TAG, "Failed to bind UDP socket to port %u: %d (%s)", this->port_, errno, strerror(errno));
    this->socket_ = nullptr;
    return;
  }

  this->active_ = true;
  ESP_LOGI(TAG, "Shelly UDP emulator listening on port %u", this->port_);
}

void ShellyEmulator::loop() {
  if (!this->active_) {
    return;
  }

  this->process_socket_();
}

float ShellyEmulator::calculate_derived_value_(float power) const {
  const float decimal_point_enforcer = 0.001f;
  if (std::isnan(power))
    power = 0.0f;

  if (std::abs(power) < 0.1f) {
    return decimal_point_enforcer;
  }

  float rounded = std::round(power);
  float ret = std::round(power * 10.0f) / 10.0f;
  if (power == rounded || power == 0.0f) {
    ret += decimal_point_enforcer;
  }
  return ret;
}

void ShellyEmulator::fill_powers_(std::vector<float> &powers) const {
  powers.clear();
  powers.reserve(this->power_sensors_.size());

  for (auto *s : this->power_sensors_) {
    float v = 0.0f;
    if (s != nullptr) {
      v = s->state;
      if (std::isnan(v))
        v = 0.0f;
    }
    powers.push_back(v);
  }

  if (powers.size() == 1) {
    powers = {powers[0], 0.0f, 0.0f};
  } else if (powers.size() != 3) {
    powers = {0.0f, 0.0f, 0.0f};
  }
}

void ShellyEmulator::process_socket_() {
  if (this->socket_ == nullptr || !this->socket_->ready()) {
    return;
  }

  int fd = this->socket_->get_fd();
  if (fd < 0) {
    return;
  }

  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  ssize_t len = recvfrom(fd, this->buffer_, sizeof(this->buffer_) - 1, MSG_DONTWAIT,
                         (struct sockaddr *) &client_addr, &client_addr_len);

  if (len < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      ESP_LOGW(TAG, "recvfrom failed: %d (%s)", errno, strerror(errno));
    }
    return;
  }

  if (len == 0) {
    return;
  }

  this->buffer_[len] = 0;

  // Parse request
  DynamicJsonDocument req_doc(768);
  DeserializationError err = deserializeJson(req_doc, (const char *) this->buffer_, len);
  if (err) {
    ESP_LOGV(TAG, "Invalid JSON: %s", err.c_str());
    return;
  }

  JsonVariant id_v = req_doc["id"];
  if (!id_v.is<int>()) {
    return;
  }
  int request_id = id_v.as<int>();

  const char *method = req_doc["method"] | "";
  JsonVariant params_id = req_doc["params"]["id"];
  if (!params_id.is<int>()) {
    // Shelly clients usually send params.id
    return;
  }

  // Gather sensor values
  std::vector<float> powers;
  this->fill_powers_(powers);

  // Build response
  DynamicJsonDocument resp_doc(768);
  resp_doc["id"] = request_id;
  resp_doc["src"] = this->device_id_;
  resp_doc["dst"] = "unknown";

  JsonObject result = resp_doc.createNestedObject("result");

  if (std::strcmp(method, "EM.GetStatus") == 0) {
    float a = this->calculate_derived_value_(powers[0]);
    float b = this->calculate_derived_value_(powers[1]);
    float c = this->calculate_derived_value_(powers[2]);

    float total = powers[0] + powers[1] + powers[2];
    float total_rounded = std::round(total * 1000.0f) / 1000.0f;
    if (total == std::round(total) || total == 0.0f) {
      total_rounded += 0.001f;
    }

    result["a_act_power"] = a;
    result["b_act_power"] = b;
    result["c_act_power"] = c;
    result["total_act_power"] = total_rounded;
  } else if (std::strcmp(method, "EM1.GetStatus") == 0) {
    float total = powers[0] + powers[1] + powers[2];
    float total_rounded = std::round(total * 1000.0f) / 1000.0f;
    if (total == std::round(total) || total == 0.0f) {
      total_rounded += 0.001f;
    }

    result["act_power"] = total_rounded;
  } else {
    // Unsupported method
    return;
  }

  char out[512];
  size_t out_len = serializeJson(resp_doc, out, sizeof(out));
  if (out_len == 0) {
    ESP_LOGW(TAG, "Failed to serialize response");
    return;
  }

  ssize_t sent = this->socket_->sendto(out, out_len, 0, (struct sockaddr *) &client_addr, client_addr_len);
  if (sent < 0) {
    ESP_LOGW(TAG, "sendto failed: %d (%s)", errno, strerror(errno));
  }
}

}  // namespace shelly_emulator
}  // namespace esphome

#endif  // USE_SHELLY_EMULATOR
