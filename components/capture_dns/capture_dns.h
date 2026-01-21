#pragma once
#include "esphome/core/defines.h"
#ifdef USE_CAPTURE_DNS
#include <memory>
#if defined(USE_ESP32)
#include "dns_server_esp32_idf.h"
#elif defined(USE_ARDUINO)
#include <DNSServer.h>
#endif
#include "esphome/core/component.h"

namespace esphome {
namespace capture_dns {

class CaptiveDns : public Component {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override {
#if defined(USE_ESP32)
    if (this->dns_server_ != nullptr) {
      this->dns_server_->process_next_request();
    }
#elif defined(USE_ARDUINO)
    if (this->dns_server_ != nullptr) {
      this->dns_server_->processNextRequest();
    }
#endif
  }
  float get_setup_priority() const override;
  void start();
  bool is_active() const { return this->active_; }
  void stop();

 protected:
  bool active_{false};
#if defined(USE_ARDUINO) || defined(USE_ESP32)
  std::unique_ptr<DNSServer> dns_server_{nullptr};
#endif
};

}  // namespace capture_dns
}  // namespace esphome
#endif
