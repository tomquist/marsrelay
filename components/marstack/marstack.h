#pragma once

#include "esphome/core/defines.h"
#ifdef USE_NETWORK
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#include <string>
#include <vector>

namespace esphome {
namespace marstack {

// A request as seen by either transport: the ESPHome web server on port 80, or
// the TLS listener on 443 (`marstack_https.cpp`). Both feed the same dispatcher
// so an endpoint is answered identically however the battery reached it.
struct Request {
  std::string method;
  std::string url;  // path only, without the query string
  std::string body;
  std::string source_ip;
  // False when the transport could not deliver the body whole (a transfer
  // encoding it cannot decode, or more bytes than max_body). The endpoint is
  // still answered -- an unacknowledged upload is what makes the battery reset
  // its network chip -- but a partial body is not decoded as telemetry.
  bool body_complete{true};
};

// The answer, in the shape the real cloud returns it.
struct Response {
  uint16_t code{404};
  const char *content_type{nullptr};
  std::string body;
  // The telemetry upload host sits behind a Kong API gateway and sends seven
  // headers the clock host does not. Reproduced for that endpoint only.
  bool kong_headers{false};
};

class MarstackRequestTrigger : public Trigger<std::string, std::string, std::string, std::string> {
 public:
  explicit MarstackRequestTrigger(void *parent) : parent_(parent) {}

 protected:
  void *parent_;
};

// Fires for a decoded telemetry upload: the device id from the URL, and the
// `d` blob decoded into a JSON object.
class MarstackVenusUploadTrigger : public Trigger<std::string, std::string> {
 public:
  explicit MarstackVenusUploadTrigger(void *parent) : parent_(parent) {}

 protected:
  void *parent_;
};

class Marstack : public Component, public AsyncWebHandler {
 public:
  explicit Marstack(web_server_base::WebServerBase *base) : base_(base) {}

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override;
  void add_request_trigger(MarstackRequestTrigger *trigger) { this->request_triggers_.push_back(trigger); }
  void add_venus_upload_trigger(MarstackVenusUploadTrigger *trigger) {
    this->venus_upload_triggers_.push_back(trigger);
  }

  void set_raw_responses(bool raw) { this->raw_responses_ = raw; }
  void set_accept_all(bool accept_all) { this->accept_all_ = accept_all; }
  void set_time_suffix(const std::string &suffix) { this->time_suffix_ = suffix; }

  /// Fire the request triggers and fill in what the cloud would answer.
  void dispatch(const Request &req, Response &res);
  /// Serialize a response the way the real cloud frames it on the wire.
  std::string build_raw_response(const Response &res) const;
  bool raw_responses() const { return this->raw_responses_; }

  /// Requests answered on either transport. The battery polls the clock
  /// endpoint on a schedule of its own, so this keeps counting even when its
  /// MQTT connection is gone -- which is what tells the two apart.
  uint32_t request_count() const { return this->request_count_; }
  /// Telemetry uploads answered (Venus on control firmware v150 and up).
  uint32_t venus_upload_count() const { return this->venus_upload_count_; }
  /// Whether any request has arrived since boot. Guards last_request(), which
  /// is meaningless before the first one.
  bool has_request() const { return this->has_request_; }
  /// millis() when the last request arrived.
  uint32_t last_request() const { return this->last_request_; }

  /// How often the diagnostic entities below are refreshed.
  void set_diagnostics_interval(uint32_t interval_ms) { this->diagnostics_interval_ms_ = interval_ms; }

#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(device_active)
  /// How long the battery may go without a request before `device_active` goes
  /// off. It polls the clock endpoint on a schedule of its own, so this is a
  /// coarser signal than the MQTT one.
  void set_device_active_timeout(uint32_t timeout_ms) { this->device_active_timeout_ms_ = timeout_ms; }
#endif

#ifdef USE_SENSOR
  SUB_SENSOR(requests)
  SUB_SENSOR(venus_uploads)
  SUB_SENSOR(request_age)
#endif

  void setup() override {
    this->base_->init();
    this->base_->add_handler(this);
  }
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

 protected:
  std::string formatted_date_string_() const;
  void publish_diagnostics_();

  web_server_base::WebServerBase *base_;
  std::vector<MarstackRequestTrigger *> request_triggers_;
  std::vector<MarstackVenusUploadTrigger *> venus_upload_triggers_;
  // Store POST body data as it arrives via onBody callback
  std::string post_body_buffer_;
  AsyncWebServerRequest *current_post_request_{nullptr};
  std::string time_suffix_{"04_0_0_0"};
  bool raw_responses_{true};
  bool accept_all_{false};
  // Both transports dispatch on the ESPHome loop task (the TLS listener hands
  // its parsed requests over through a queue), so these need no locking.
  uint32_t request_count_{0};
  uint32_t venus_upload_count_{0};
  uint32_t last_request_{0};
  bool has_request_{false};
  uint32_t diagnostics_interval_ms_{60000};
  uint32_t last_diagnostics_{0};
  bool diagnostics_started_{false};
#ifdef USE_BINARY_SENSOR
  uint32_t device_active_timeout_ms_{1800000};
#endif
};

/// True for "/data-upload/v1/venus/<id>": the battery appends its device id as
/// the final path segment, so this matches one non-empty segment after the
/// prefix rather than an exact path.
bool is_data_upload_path(const std::string &url);
/// The device id from such a path, or "" when the path is not an upload path.
std::string data_upload_device_id(const std::string &url);
/// True for the clock endpoint. The battery has used several spellings
/// ("getDateInfoeu.php", "getDateInfo.php"), so match the common stem.
bool is_time_path(const std::string &url);

/// Decode the `d` blob of a telemetry upload into a JSON object. `body` is the
/// raw POST body; returns "" when it carries no `d` field.
std::string decode_venus_telemetry(const std::string &body);

}  // namespace marstack
}  // namespace esphome
#endif
