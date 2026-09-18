#pragma once
#include "esphome/core/defines.h"
#ifdef USE_UDP_PROXY

#include <memory>
#include <map>
#include "esphome/core/component.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/network/ip_address.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

namespace esphome {
namespace udp_proxy {

/// Represents an active UDP session that tracks a client's source port
/// so responses can be routed back correctly.
struct UdpSession {
  /// The original client's IP address (on AP network)
  network::IPAddress client_ip;
  /// The original client's source port
  uint16_t client_port;
  /// Timestamp when this session was last active
  uint32_t last_activity;
};

class UdpProxy : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_port(uint16_t port) { this->port_ = port; }
  void set_session_timeout(uint32_t timeout_ms) { this->session_timeout_ms_ = timeout_ms; }

  void start();
  void stop();
  /// True once both sockets are bound. A proxy whose bind failed -- or that
  /// came up before the network did -- reports false here until a retry gets
  /// it running.
  bool is_active() const { return this->active_; }

  /// Packets forwarded from the battery's network to the home network, i.e.
  /// the meter discovery requests the battery sends.
  uint32_t packets_to_sta() const { return this->packets_to_sta_; }
  /// Packets forwarded back from the home network to the battery, i.e. the
  /// meter's answers.
  uint32_t packets_to_ap() const { return this->packets_to_ap_; }
  /// Packets received but not forwarded: from outside the AP subnet, with no
  /// session to answer, or whose forward failed.
  uint32_t packets_dropped() const { return this->packets_dropped_; }
  /// Clients with a session that has not timed out yet.
  size_t session_count() const { return this->sessions_.size(); }
  /// Whether a request/response has been seen at all since boot. Guards the
  /// timestamps below, which are meaningless before the first one.
  bool has_request() const { return this->has_request_; }
  bool has_response() const { return this->has_response_; }
  /// millis() of the last packet in either direction.
  uint32_t last_request() const { return this->last_request_; }
  uint32_t last_response() const { return this->last_response_; }

  /// How often the diagnostic entities below are refreshed.
  void set_diagnostics_interval(uint32_t interval_ms) { this->diagnostics_interval_ms_ = interval_ms; }

#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(active)
  SUB_BINARY_SENSOR(meter_responding)
  /// How long the meter may stay quiet before `meter_responding` goes off.
  void set_meter_timeout(uint32_t timeout_ms) { this->meter_timeout_ms_ = timeout_ms; }
#endif

#ifdef USE_SENSOR
  SUB_SENSOR(packets_to_sta)
  SUB_SENSOR(packets_to_ap)
  SUB_SENSOR(packets_dropped)
  SUB_SENSOR(sessions)
  SUB_SENSOR(request_age)
  SUB_SENSOR(response_age)
#endif

 protected:
  /// Process incoming packets on the AP-side socket
  void process_ap_socket();

  /// Process incoming packets on the STA-side socket
  void process_sta_socket();

  /// Clean up expired sessions
  void cleanup_expired_sessions();

  /// Try start() again, with a growing delay, while the proxy is not active.
  void retry_start();

  /// Refresh whatever diagnostic entities are configured.
  void publish_diagnostics();

  /// Check if an IP address belongs to the AP subnet
  bool is_ap_network(const network::IPAddress &ip);

  /// Get the AP network's IP address
  network::IPAddress get_ap_ip();

  /// Get the STA network's IP address
  network::IPAddress get_sta_ip();

  /// Buffer size for UDP packets
  static constexpr size_t UDP_BUFFER_SIZE = 1500;

  /// Bounds for the delay between start() attempts
  static constexpr uint32_t START_RETRY_MIN_MS = 5000;
  static constexpr uint32_t START_RETRY_MAX_MS = 60000;

  /// Target port to listen on and forward to
  uint16_t port_{0};

  /// Session timeout in milliseconds
  uint32_t session_timeout_ms_{30000};

  /// Whether the proxy is active
  bool active_{false};

  /// Socket listening on the AP network (receives from Marstek device)
  std::unique_ptr<socket::ListenSocket> ap_socket_{nullptr};

  /// Socket for STA network communication (forwards to home network, receives responses)
  std::unique_ptr<socket::ListenSocket> sta_socket_{nullptr};

  /// Map of STA-side source port to session info for routing responses back
  /// Key is a session identifier (we use a simple incrementing counter)
  /// In practice, since we might have multiple AP clients, we track by remote port
  std::map<uint16_t, UdpSession> sessions_;

  /// Receive buffer
  uint8_t buffer_[UDP_BUFFER_SIZE];

  /// Counter for cleanup cycle
  uint32_t last_cleanup_{0};

  /// When start() was last attempted, and how long to wait before trying again
  uint32_t last_start_attempt_{0};
  uint32_t start_retry_delay_ms_{START_RETRY_MIN_MS};

  /// Traffic counters, exposed for diagnostics
  uint32_t packets_to_sta_{0};
  uint32_t packets_to_ap_{0};
  uint32_t packets_dropped_{0};
  uint32_t last_request_{0};
  uint32_t last_response_{0};
  bool has_request_{false};
  bool has_response_{false};

  /// Diagnostics refresh bookkeeping
  uint32_t diagnostics_interval_ms_{60000};
  uint32_t last_diagnostics_{0};
  bool diagnostics_started_{false};
#ifdef USE_BINARY_SENSOR
  uint32_t meter_timeout_ms_{300000};
#endif
};

}  // namespace udp_proxy
}  // namespace esphome

#endif  // USE_UDP_PROXY
