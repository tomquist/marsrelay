#pragma once

#include "esphome/core/defines.h"
#ifdef USE_NETWORK
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <vector>

namespace esphome {
namespace marstack {

class MarstackRequestTrigger : public Trigger<std::string, std::string, std::string, std::string> {
 public:
  explicit MarstackRequestTrigger(void *parent) : parent_(parent) {}

 protected:
  void *parent_;
};

class Marstack : public Component, public AsyncWebHandler {
 public:
  explicit Marstack(web_server_base::WebServerBase *base) : base_(base) {}

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void onBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
  void add_request_trigger(MarstackRequestTrigger *trigger) { this->request_triggers_.push_back(trigger); }

  void setup() override {
    this->base_->init();
    this->base_->add_handler(this);
  }
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

 protected:
  web_server_base::WebServerBase *base_;
  std::vector<MarstackRequestTrigger *> request_triggers_;
  // Store POST body data as it arrives via onBody callback
  std::string post_body_buffer_;
  AsyncWebServerRequest *current_post_request_{nullptr};
};

}  // namespace marstack
}  // namespace esphome
#endif
