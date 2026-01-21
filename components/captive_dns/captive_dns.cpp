#include "captive_dns.h"
#ifdef USE_CAPTIVE_DNS
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome {
namespace captive_dns {

static const char *const TAG = "captive_dns";

void CaptiveDns::setup() { this->start(); }

void CaptiveDns::start() {
  if (this->active_) {
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
  ESP_LOGV(TAG, "Captive DNS started");
}

void CaptiveDns::stop() {
  if (!this->active_) {
    return;
  }

  this->active_ = false;
  if (this->dns_server_ != nullptr) {
    this->dns_server_->stop();
    this->dns_server_ = nullptr;
  }
  ESP_LOGV(TAG, "Captive DNS stopped");
}

float CaptiveDns::get_setup_priority() const {
  // After WiFi
  return setup_priority::WIFI - 1.0f;
}

void CaptiveDns::dump_config() { ESP_LOGCONFIG(TAG, "Captive DNS:"); }

}  // namespace captive_dns
}  // namespace esphome
#endif
