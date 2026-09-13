#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32) && defined(USE_ESP_IDF)

#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "marstack.h"

extern "C" {
#include "esp_tls.h"
}

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace esphome {
namespace marstack {

/// The cloud's HTTPS side.
///
/// Since Venus control firmware v150 the battery uploads its telemetry to
/// `https://api-<region>.marstekcloud.com/data-upload/v1/venus/<id>` instead of
/// sending it in the clear, and it buffers every record the upload does not
/// acknowledge. Once that backlog is non-empty the firmware hardware-resets its
/// own network chip on a fixed timer — 900 s on WiFi, which is how a battery
/// talks to Marsrelay — and during the reset Modbus, MQTT and even ping stop
/// for a few seconds. Answering the upload keeps the backlog empty, so the
/// reset never fires.
///
/// The ESPHome web server cannot serve this: it is plain HTTP, and the reply
/// has to be framed exactly the way the real cloud frames it (see
/// Marstack::build_raw_response). So this runs its own TLS listener.
class MarstackHttps : public Component {
 public:
  explicit MarstackHttps(Marstack *parent) : parent_(parent) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_port(uint16_t port) { this->port_ = port; }
  void set_accept_all(bool accept_all) { this->accept_all_ = accept_all; }
  void set_hold_time(uint32_t hold_time_ms) { this->hold_time_ms_ = hold_time_ms; }
  void set_max_body(uint32_t max_body) { this->max_body_ = max_body; }
  void set_max_connections(uint8_t max_connections) { this->max_connections_ = max_connections; }

 protected:
  // A request handed from the TLS task to the ESPHome main loop and back, so
  // that automations run where every other ESPHome automation runs.
  struct Handoff {
    const Request *request;
    Response *response;
  };

  // A connection that has been answered and is being held open. The firmware
  // reads the TLS reply with a loop whose only early exit is our close_notify;
  // cutting the connection before its own 20 s receive timeout makes it treat
  // the error as a byte count and discard the reply, which leaves the record in
  // the upload buffer — the very thing this component exists to prevent. So an
  // answered connection is held past that timeout and then ended cleanly.
  struct HeldConnection {
    int fd;
    esp_tls_t *tls;
    uint32_t close_at;
  };

  static void task_entry_(void *param);
  void task_loop_();
  bool start_listener_();
  void accept_connection_();
  void serve_connection_(int fd, const std::string &peer);
  bool read_request_(esp_tls_t *tls, Request &req);
  bool write_all_(esp_tls_t *tls, const std::string &data);
  void hold_or_close_(int fd, esp_tls_t *tls);
  void close_session_(int fd, esp_tls_t *tls);
  void reap_held_();

  Marstack *parent_;
  uint16_t port_{443};
  bool accept_all_{false};
  uint32_t hold_time_ms_{25000};
  uint32_t max_body_{8192};
  uint8_t max_connections_{4};

  int listen_fd_{-1};
  bool start_requested_{false};
  TaskHandle_t task_handle_{nullptr};
  QueueHandle_t request_queue_{nullptr};
  QueueHandle_t response_queue_{nullptr};
  std::vector<HeldConnection> held_;
};

}  // namespace marstack
}  // namespace esphome

#endif
