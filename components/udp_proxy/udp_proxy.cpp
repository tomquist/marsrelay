#include "udp_proxy.h"

#ifdef USE_UDP_PROXY

#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/wifi/wifi_component.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/igmp.h>
#include <esp_netif.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace esphome {
namespace udp_proxy {

static const char *const TAG = "udp_proxy";

void UdpProxy::setup() {
  this->start();
  this->last_start_attempt_ = millis();
}

float UdpProxy::get_setup_priority() const {
  // After WiFi
  return setup_priority::WIFI - 1.0f;
}

void UdpProxy::dump_config() {
  ESP_LOGCONFIG(TAG, "UDP Proxy:");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  ESP_LOGCONFIG(TAG, "  Meter Network: %s", this->meter_on_ap_ ? "access point" : "home");
  ESP_LOGCONFIG(TAG, "  Session Timeout: %d ms", this->session_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Active: %s", this->active_ ? "Yes" : "No");
}

network::IPAddress UdpProxy::get_ap_ip() {
  return wifi::global_wifi_component->wifi_soft_ap_ip();
}

network::IPAddress UdpProxy::get_sta_ip() {
  network::IPAddresses addresses = wifi::global_wifi_component->get_ip_addresses();
  return addresses[0];
}

bool UdpProxy::is_ap_network(const network::IPAddress &ip) {
  // Check if the IP is in the AP subnet (typically 192.168.4.x)
  network::IPAddress ap_ip = this->get_ap_ip();

  // Compare first 3 octets (assuming /24 subnet)
  // For IPv4, we can compare the network portion
  ip4_addr_t ap_addr = ap_ip;
  ip4_addr_t check_addr = ip;

  // Mask for /24 network in network byte order
  // 255.255.255.0 = 0xFFFFFF00 in host order, need htonl to convert to network order
  uint32_t mask = htonl(0xFFFFFF00);
  return (ap_addr.addr & mask) == (check_addr.addr & mask);
}

uint32_t UdpProxy::request_destination() {
  if (!this->meter_on_ap_) {
    // Leaves through the default interface, which is the STA one.
    return INADDR_BROADCAST;  // 255.255.255.255
  }
  // 255.255.255.255 would also leave through the STA interface. The AP
  // subnet's own broadcast address is routed to the AP. Same /24 assumption
  // as is_ap_network().
  ip4_addr_t ap_addr = this->get_ap_ip();
  return ap_addr.addr | htonl(0x000000FF);
}

void UdpProxy::start() {
  if (this->active_) {
    ESP_LOGW(TAG, "UDP proxy already active");
    return;
  }

  if (this->port_ == 0) {
    ESP_LOGE(TAG, "No port configured for UDP proxy");
    return;
  }

  // Wait for both AP and STA to be ready
  network::IPAddress ap_ip = this->get_ap_ip();
  network::IPAddress sta_ip = this->get_sta_ip();

  char ap_ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
  char sta_ip_buf[network::IP_ADDRESS_BUFFER_SIZE];

  ESP_LOGI(TAG, "Starting UDP proxy on port %d", this->port_);
  ESP_LOGI(TAG, "  AP IP: %s", ap_ip.str_to(ap_ip_buf));
  ESP_LOGI(TAG, "  STA IP: %s", sta_ip.str_to(sta_ip_buf));

  // Create the listen socket - this receives the batteries' broadcasts
  this->listen_socket_ = socket::socket_ip_loop_monitored(SOCK_DGRAM, IPPROTO_UDP);
  if (this->listen_socket_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create listen UDP socket");
    return;
  }

  int enable = 1;
  this->listen_socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  this->listen_socket_->setsockopt(SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

  // Bind to all interfaces on the target port
  // We'll filter by source IP to determine which network the packet came from
  struct sockaddr_storage listen_addr = {};
  socklen_t listen_addr_len =
      socket::set_sockaddr_any((struct sockaddr *) &listen_addr, sizeof(listen_addr), this->port_);

  int err = this->listen_socket_->bind((struct sockaddr *) &listen_addr, listen_addr_len);
  if (err != 0) {
    ESP_LOGE(TAG, "Failed to bind listen socket to port %d: %d (%s)", this->port_, errno, strerror(errno));
    this->listen_socket_ = nullptr;
    return;
  }

  ESP_LOGI(TAG, "Listen socket bound to port %d", this->port_);

  // Create the forward socket - this forwards to the meter's network and receives responses
  this->forward_socket_ = socket::socket_ip_loop_monitored(SOCK_DGRAM, IPPROTO_UDP);
  if (this->forward_socket_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create forward UDP socket");
    this->listen_socket_->close();
    this->listen_socket_ = nullptr;
    return;
  }

  this->forward_socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  this->forward_socket_->setsockopt(SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

  // Bind to any available port for outgoing traffic
  // We bind to port 0 to let the system assign a port
  struct sockaddr_storage forward_addr = {};
  socklen_t forward_addr_len = socket::set_sockaddr_any((struct sockaddr *) &forward_addr, sizeof(forward_addr), 0);

  err = this->forward_socket_->bind((struct sockaddr *) &forward_addr, forward_addr_len);
  if (err != 0) {
    ESP_LOGE(TAG, "Failed to bind forward socket: %d (%s)", errno, strerror(errno));
    this->listen_socket_->close();
    this->listen_socket_ = nullptr;
    this->forward_socket_ = nullptr;
    return;
  }

  // Get the assigned port for logging
  struct sockaddr_in local_addr;
  socklen_t local_addr_len = sizeof(local_addr);
  getsockname(this->forward_socket_->get_fd(), (struct sockaddr *) &local_addr, &local_addr_len);
  ESP_LOGI(TAG, "Forward socket bound to port %d", ntohs(local_addr.sin_port));

  this->active_ = true;
  ESP_LOGI(TAG, "UDP proxy started successfully");
}

void UdpProxy::stop() {
  if (!this->active_) {
    return;
  }

  this->active_ = false;

  if (this->listen_socket_ != nullptr) {
    this->listen_socket_->close();
    this->listen_socket_ = nullptr;
  }

  if (this->forward_socket_ != nullptr) {
    this->forward_socket_->close();
    this->forward_socket_ = nullptr;
  }

  this->sessions_.clear();
  ESP_LOGI(TAG, "UDP proxy stopped");
}

void UdpProxy::loop() {
  const uint32_t now = millis();

  // Evaluated every loop, ahead of everything that can return early, and
  // independent of the diagnostics tick: an automation should still fire when
  // `diagnostics_interval` is long, or `never`.
  for (auto *trigger : this->liveness_triggers_) {
    trigger->update(this->has_response_, this->last_response_);
  }

  if (!this->active_) {
    // setup() runs before the network is necessarily up, and a bind can fail
    // for reasons that pass. Without this the proxy would stay down until the
    // next reboot, with nothing but one log line to say so.
    this->retry_start();
  }

  // After the retry above, so a recovery shows up in the entities right away,
  // and ahead of the return below, so a proxy that is still down publishes
  // that.
  if (this->diagnostics_interval_ms_ != SCHEDULER_DONT_RUN &&  // `never`
      now - this->last_diagnostics_ >= (this->diagnostics_started_ ? this->diagnostics_interval_ms_ : 5000)) {
    this->last_diagnostics_ = now;
    this->diagnostics_started_ = true;
    this->publish_diagnostics();
  }

  if (!this->active_) {
    return;
  }

  // Process incoming packets on both sockets
  this->process_requests();
  this->process_responses();

  // Cleanup expired sessions periodically (every 5 seconds)
  if (now - this->last_cleanup_ > 5000) {
    this->cleanup_expired_sessions();
    this->last_cleanup_ = now;
  }
}

void UdpProxy::publish_diagnostics() {
#ifdef USE_BINARY_SENSOR
  if (this->active_binary_sensor_ != nullptr) {
    this->active_binary_sensor_->publish_state(this->active_);
  }
  if (this->meter_responding_binary_sensor_ != nullptr) {
    const bool responding =
        this->has_response_ && millis() - this->last_response_ < this->meter_timeout_ms_;
    this->meter_responding_binary_sensor_->publish_state(responding);
  }
#endif

#ifdef USE_SENSOR
  if (this->packets_to_sta_sensor_ != nullptr) {
    this->packets_to_sta_sensor_->publish_state((float) this->packets_to_sta_);
  }
  if (this->packets_to_ap_sensor_ != nullptr) {
    this->packets_to_ap_sensor_->publish_state((float) this->packets_to_ap_);
  }
  if (this->packets_dropped_sensor_ != nullptr) {
    this->packets_dropped_sensor_->publish_state((float) this->packets_dropped_);
  }
  if (this->sessions_sensor_ != nullptr) {
    this->sessions_sensor_->publish_state((float) this->sessions_.size());
  }
  // NAN until the first packet of that kind: unknown, not zero.
  if (this->request_age_sensor_ != nullptr) {
    this->request_age_sensor_->publish_state(this->has_request_ ? (millis() - this->last_request_) / 1000.0f
                                                                : NAN);
  }
  if (this->response_age_sensor_ != nullptr) {
    this->response_age_sensor_->publish_state(this->has_response_ ? (millis() - this->last_response_) / 1000.0f
                                                                  : NAN);
  }
#endif
}

void UdpProxy::retry_start() {
  const uint32_t now = millis();
  if (now - this->last_start_attempt_ < this->start_retry_delay_ms_) {
    return;
  }
  this->last_start_attempt_ = now;
  this->start();
  if (this->active_) {
    this->start_retry_delay_ms_ = START_RETRY_MIN_MS;
  } else {
    // start() has already logged why. Back off so a proxy that cannot bind at
    // all does not fill the log with the same line every five seconds.
    this->start_retry_delay_ms_ = std::min<uint32_t>(this->start_retry_delay_ms_ * 2, START_RETRY_MAX_MS);
  }
}

void UdpProxy::process_requests() {
  if (this->listen_socket_ == nullptr || !this->listen_socket_->ready()) {
    return;
  }

  int fd = this->listen_socket_->get_fd();
  if (fd < 0) {
    return;
  }

  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  ssize_t len = recvfrom(fd, this->buffer_, sizeof(this->buffer_), MSG_DONTWAIT,
                         (struct sockaddr *) &client_addr, &client_addr_len);

  if (len < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      ESP_LOGE(TAG, "Listen socket recvfrom failed: %d (%s)", errno, strerror(errno));
    }
    return;
  }

  if (len == 0) {
    return;
  }

  ip_addr_t src_ip_addr;
  IP_ADDR4(&src_ip_addr,
           (client_addr.sin_addr.s_addr >> 0) & 0xFF,
           (client_addr.sin_addr.s_addr >> 8) & 0xFF,
           (client_addr.sin_addr.s_addr >> 16) & 0xFF,
           (client_addr.sin_addr.s_addr >> 24) & 0xFF);
  network::IPAddress src_ip(&src_ip_addr);
  uint16_t src_port = ntohs(client_addr.sin_port);

  char src_ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
  ESP_LOGD(TAG, "Received %zd bytes on listen socket from %s:%d", len, src_ip.str_to(src_ip_buf), src_port);

  // Requests come from the side the meter is not on. The listen socket is
  // bound to all interfaces, so it also sees broadcasts from the meter's side.
  if (this->is_ap_network(src_ip) == this->meter_on_ap_) {
    ESP_LOGV(TAG, "Ignoring packet from the meter's network");
    this->packets_dropped_++;
    return;
  }

  this->last_request_ = millis();
  this->has_request_ = true;

  // Store/update session for this client
  UdpSession session;
  session.client_ip = src_ip;
  session.client_port = src_port;
  session.last_activity = millis();
  this->sessions_[(uint64_t(client_addr.sin_addr.s_addr) << 16) | src_port] = session;

  ESP_LOGD(TAG, "Session created/updated for %s:%d", src_ip_buf, src_port);

  // Forward to the meter's network as broadcast
  if (this->forward_socket_ == nullptr) {
    ESP_LOGW(TAG, "Forward socket not available, cannot forward");
    this->packets_dropped_++;
    return;
  }

  struct sockaddr_in broadcast_addr;
  memset(&broadcast_addr, 0, sizeof(broadcast_addr));
  broadcast_addr.sin_family = AF_INET;
  broadcast_addr.sin_port = htons(this->port_);
  broadcast_addr.sin_addr.s_addr = this->request_destination();

  const char *meter_network = this->meter_on_ap_ ? "AP" : "STA";
  ssize_t sent = this->forward_socket_->sendto(this->buffer_, len, 0,
                                                (struct sockaddr *) &broadcast_addr, sizeof(broadcast_addr));

  if (sent < 0) {
    ESP_LOGE(TAG, "Failed to forward to %s network: %d (%s)", meter_network, errno, strerror(errno));
    this->packets_dropped_++;
  } else {
    ESP_LOGD(TAG, "Forwarded %zd bytes to %s network broadcast", sent, meter_network);
    if (this->meter_on_ap_) {
      this->packets_to_ap_++;
    } else {
      this->packets_to_sta_++;
    }
  }
}

void UdpProxy::process_responses() {
  if (this->forward_socket_ == nullptr || !this->forward_socket_->ready()) {
    return;
  }

  int fd = this->forward_socket_->get_fd();
  if (fd < 0) {
    return;
  }

  struct sockaddr_in sender_addr;
  socklen_t sender_addr_len = sizeof(sender_addr);

  ssize_t len = recvfrom(fd, this->buffer_, sizeof(this->buffer_), MSG_DONTWAIT,
                         (struct sockaddr *) &sender_addr, &sender_addr_len);

  if (len < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      ESP_LOGE(TAG, "Forward socket recvfrom failed: %d (%s)", errno, strerror(errno));
    }
    return;
  }

  if (len == 0) {
    return;
  }

  ip_addr_t src_ip_addr;
  IP_ADDR4(&src_ip_addr,
           (sender_addr.sin_addr.s_addr >> 0) & 0xFF,
           (sender_addr.sin_addr.s_addr >> 8) & 0xFF,
           (sender_addr.sin_addr.s_addr >> 16) & 0xFF,
           (sender_addr.sin_addr.s_addr >> 24) & 0xFF);
  network::IPAddress src_ip(&src_ip_addr);
  uint16_t src_port = ntohs(sender_addr.sin_port);

  char src_ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
  ESP_LOGD(TAG, "Received %zd bytes on forward socket from %s:%d", len, src_ip.str_to(src_ip_buf), src_port);

  // A meter on the AP answers from the AP. Anything else reaching the
  // forward socket is not an answer to forward to the home network.
  if (this->meter_on_ap_ && !this->is_ap_network(src_ip)) {
    ESP_LOGV(TAG, "Ignoring packet from outside the meter's network");
    this->packets_dropped_++;
    return;
  }

  this->last_response_ = millis();
  this->has_response_ = true;

  // Forward response to all active clients
  if (this->sessions_.empty()) {
    ESP_LOGV(TAG, "No active sessions, dropping response");
    this->packets_dropped_++;
    return;
  }

  if (this->listen_socket_ == nullptr) {
    ESP_LOGW(TAG, "Listen socket not available, cannot forward response");
    this->packets_dropped_++;
    return;
  }

  int forwarded_count = 0;
  for (auto &kv : this->sessions_) {
    UdpSession &session = kv.second;

    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(session.client_port);

    ip4_addr_t addr = session.client_ip;
    client_addr.sin_addr.s_addr = addr.addr;

    char client_ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
    ESP_LOGD(TAG, "Forwarding response to client %s:%d",
             session.client_ip.str_to(client_ip_buf), session.client_port);

    ssize_t sent = this->listen_socket_->sendto(this->buffer_, len, 0,
                                                 (struct sockaddr *) &client_addr, sizeof(client_addr));

    if (sent < 0) {
      ESP_LOGE(TAG, "Failed to forward to client: %d (%s)", errno, strerror(errno));
      this->packets_dropped_++;
    } else {
      forwarded_count++;
      if (this->meter_on_ap_) {
        this->packets_to_sta_++;
      } else {
        this->packets_to_ap_++;
      }
      // Update session activity
      session.last_activity = millis();
    }
  }

  ESP_LOGD(TAG, "Forwarded response to %d client(s)", forwarded_count);
}

void UdpProxy::cleanup_expired_sessions() {
  uint32_t now = millis();
  auto it = this->sessions_.begin();

  while (it != this->sessions_.end()) {
    if (now - it->second.last_activity > this->session_timeout_ms_) {
      char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
      ESP_LOGD(TAG, "Session expired for %s:%d",
               it->second.client_ip.str_to(ip_buf), it->second.client_port);
      it = this->sessions_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace udp_proxy
}  // namespace esphome

#endif  // USE_UDP_PROXY
