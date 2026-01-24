#pragma once
#include "esphome/core/defines.h"
#ifdef USE_UDP_PROXY

#include <memory>
#include <map>
#include "esphome/core/component.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/network/ip_address.h"

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
  bool is_active() const { return this->active_; }

 protected:
  /// Process incoming packets on the AP-side socket
  void process_ap_socket();

  /// Process incoming packets on the STA-side socket
  void process_sta_socket();

  /// Clean up expired sessions
  void cleanup_expired_sessions();

  /// Check if an IP address belongs to the AP subnet
  bool is_ap_network(const network::IPAddress &ip);

  /// Get the AP network's IP address
  network::IPAddress get_ap_ip();

  /// Get the STA network's IP address
  network::IPAddress get_sta_ip();

  /// Buffer size for UDP packets
  static constexpr size_t UDP_BUFFER_SIZE = 1500;

  /// Target port to listen on and forward to
  uint16_t port_{0};

  /// Session timeout in milliseconds
  uint32_t session_timeout_ms_{30000};

  /// Whether the proxy is active
  bool active_{false};

  /// Socket listening on the AP network (receives from Marstek device)
  std::unique_ptr<socket::Socket> ap_socket_{nullptr};

  /// Socket for STA network communication (forwards to home network, receives responses)
  std::unique_ptr<socket::Socket> sta_socket_{nullptr};

  /// Map of STA-side source port to session info for routing responses back
  /// Key is a session identifier (we use a simple incrementing counter)
  /// In practice, since we might have multiple AP clients, we track by remote port
  std::map<uint16_t, UdpSession> sessions_;

  /// Receive buffer
  uint8_t buffer_[UDP_BUFFER_SIZE];

  /// Counter for cleanup cycle
  uint32_t last_cleanup_{0};
};

}  // namespace udp_proxy
}  // namespace esphome

#endif  // USE_UDP_PROXY
