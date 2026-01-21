#pragma once

#include "esphome/core/defines.h"
#ifdef USE_NETWORK
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <vector>

namespace esphome {
namespace marstack_webserver {

class MarstackRequestTrigger : public Trigger<std::string, std::string, std::string, std::string> {
 public:
  explicit MarstackRequestTrigger(void *parent) : parent_(parent) {}

 protected:
  void *parent_;
};

class MarstackWebServer : public Component, public AsyncWebHandler {
 public:
  explicit MarstackWebServer(web_server_base::WebServerBase *base) : base_(base) {}

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void add_request_trigger(MarstackRequestTrigger *trigger) { this->request_triggers_.push_back(trigger); }

  void setup() override {
    this->base_->init();
    this->base_->add_handler(this);
  }
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

 protected:
  web_server_base::WebServerBase *base_;
  std::vector<MarstackRequestTrigger *> request_triggers_;
};

}  // namespace marstack_webserver
}  // namespace esphome
#endif
