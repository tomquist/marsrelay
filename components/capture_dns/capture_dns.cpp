#include "capture_dns.h"
#ifdef USE_CAPTURE_DNS
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/components/network/ip_address.h"

namespace esphome {
namespace capture_dns {

static const char *const TAG = "capture_dns";

void CaptiveDns::setup() { this->start(); }

void CaptiveDns::start() {
  if (this->active_) {
    ESP_LOGW(TAG, "DNS server already active");
    return;
  }

  network::IPAddress ip = wifi::global_wifi_component->wifi_soft_ap_ip();

#if defined(USE_ESP32)
  this->dns_server_ = make_unique<DNSServer>();
  this->dns_server_->start(ip);
#elif defined(USE_ARDUINO)
  this->dns_server_ = make_unique<DNSServer>();
  this->dns_server_->setErrorReplyCode(DNSReplyCode::NoError);
  this->dns_server_->start(53, ESPHOME_F("*"), ip);
#endif

  this->active_ = true;
  char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
  ESP_LOGI(TAG, "Captive DNS server started, responding with IP: %s", ip.str_to(ip_buf));
}

void CaptiveDns::stop() {
  if (!this->active_) {
    ESP_LOGV(TAG, "DNS server already stopped");
    return;
  }

  this->active_ = false;
  if (this->dns_server_ != nullptr) {
    this->dns_server_->stop();
    this->dns_server_ = nullptr;
  }
  ESP_LOGI(TAG, "Captive DNS server stopped");
}

float CaptiveDns::get_setup_priority() const {
  // After WiFi
  return setup_priority::WIFI - 1.0f;
}

void CaptiveDns::dump_config() {
  ESP_LOGCONFIG(TAG, "Captive DNS:");
  ESP_LOGCONFIG(TAG, "  Active: %s", this->active_ ? "Yes" : "No");
  if (this->active_) {
    network::IPAddress ip = wifi::global_wifi_component->wifi_soft_ap_ip();
    char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
    ESP_LOGCONFIG(TAG, "  Response IP: %s", ip.str_to(ip_buf));
  }
}

}  // namespace capture_dns
}  // namespace esphome
#endif
