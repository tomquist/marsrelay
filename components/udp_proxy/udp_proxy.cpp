#include "udp_proxy.h"

#ifdef USE_UDP_PROXY

#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/wifi/wifi_component.h"
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/igmp.h>
#include <esp_netif.h>
#include <cstring>

namespace esphome {
namespace udp_proxy {

static const char *const TAG = "udp_proxy";

void UdpProxy::setup() {
  this->start();
}

float UdpProxy::get_setup_priority() const {
  // After WiFi
  return setup_priority::WIFI - 1.0f;
}

void UdpProxy::dump_config() {
  ESP_LOGCONFIG(TAG, "UDP Proxy:");
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
  ESP_LOGCONFIG(TAG, "  Session Timeout: %d ms", this->session_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Active: %s", this->active_ ? "Yes" : "No");
}

network::IPAddress UdpProxy::get_ap_ip() {
  return wifi::global_wifi_component->wifi_soft_ap_ip();
}

network::IPAddress UdpProxy::get_sta_ip() {
  return wifi::global_wifi_component->wifi_sta_ip();
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

  // Create AP-side socket - this receives broadcasts from Marstek device
  this->ap_socket_ = socket::socket_ip_loop_monitored(SOCK_DGRAM, IPPROTO_UDP);
  if (this->ap_socket_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create AP-side UDP socket");
    return;
  }

  int enable = 1;
  this->ap_socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  this->ap_socket_->setsockopt(SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

  // Bind to all interfaces on the target port
  // We'll filter by source IP to determine which network the packet came from
  struct sockaddr_storage ap_addr = {};
  socklen_t ap_addr_len = socket::set_sockaddr_any((struct sockaddr *) &ap_addr, sizeof(ap_addr), this->port_);

  int err = this->ap_socket_->bind((struct sockaddr *) &ap_addr, ap_addr_len);
  if (err != 0) {
    ESP_LOGE(TAG, "Failed to bind AP socket to port %d: %d (%s)", this->port_, errno, strerror(errno));
    this->ap_socket_ = nullptr;
    return;
  }

  ESP_LOGI(TAG, "AP socket bound to port %d", this->port_);

  // Create STA-side socket - this forwards to home network and receives responses
  this->sta_socket_ = socket::socket_ip_loop_monitored(SOCK_DGRAM, IPPROTO_UDP);
  if (this->sta_socket_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create STA-side UDP socket");
    this->ap_socket_->close();
    this->ap_socket_ = nullptr;
    return;
  }

  this->sta_socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  this->sta_socket_->setsockopt(SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

  // Bind to any available port on STA interface for outgoing traffic
  // We bind to port 0 to let the system assign a port
  struct sockaddr_storage sta_addr = {};
  socklen_t sta_addr_len = socket::set_sockaddr_any((struct sockaddr *) &sta_addr, sizeof(sta_addr), 0);

  err = this->sta_socket_->bind((struct sockaddr *) &sta_addr, sta_addr_len);
  if (err != 0) {
    ESP_LOGE(TAG, "Failed to bind STA socket: %d (%s)", errno, strerror(errno));
    this->ap_socket_->close();
    this->ap_socket_ = nullptr;
    this->sta_socket_ = nullptr;
    return;
  }

  // Get the assigned port for logging
  struct sockaddr_in local_addr;
  socklen_t local_addr_len = sizeof(local_addr);
  getsockname(this->sta_socket_->get_fd(), (struct sockaddr *) &local_addr, &local_addr_len);
  ESP_LOGI(TAG, "STA socket bound to port %d", ntohs(local_addr.sin_port));

  this->active_ = true;
  ESP_LOGI(TAG, "UDP proxy started successfully");
}

void UdpProxy::stop() {
  if (!this->active_) {
    return;
  }

  this->active_ = false;

  if (this->ap_socket_ != nullptr) {
    this->ap_socket_->close();
    this->ap_socket_ = nullptr;
  }

  if (this->sta_socket_ != nullptr) {
    this->sta_socket_->close();
    this->sta_socket_ = nullptr;
  }

  this->sessions_.clear();
  ESP_LOGI(TAG, "UDP proxy stopped");
}

void UdpProxy::loop() {
  if (!this->active_) {
    return;
  }

  // Process incoming packets on both sockets
  this->process_ap_socket();
  this->process_sta_socket();

  // Cleanup expired sessions periodically (every 5 seconds)
  uint32_t now = millis();
  if (now - this->last_cleanup_ > 5000) {
    this->cleanup_expired_sessions();
    this->last_cleanup_ = now;
  }
}

void UdpProxy::process_ap_socket() {
  if (this->ap_socket_ == nullptr || !this->ap_socket_->ready()) {
    return;
  }

  int fd = this->ap_socket_->get_fd();
  if (fd < 0) {
    return;
  }

  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  ssize_t len = recvfrom(fd, this->buffer_, sizeof(this->buffer_), MSG_DONTWAIT,
                         (struct sockaddr *) &client_addr, &client_addr_len);

  if (len < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      ESP_LOGE(TAG, "AP socket recvfrom failed: %d (%s)", errno, strerror(errno));
    }
    return;
  }

  if (len == 0) {
    return;
  }

  network::IPAddress src_ip;
  src_ip = client_addr.sin_addr.s_addr;
  uint16_t src_port = ntohs(client_addr.sin_port);

  char src_ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
  ESP_LOGD(TAG, "Received %d bytes on AP socket from %s:%d", len, src_ip.str_to(src_ip_buf), src_port);

  // Check if this is from the AP network (our target)
  if (!this->is_ap_network(src_ip)) {
    ESP_LOGV(TAG, "Ignoring packet from non-AP network");
    return;
  }

  // Store/update session for this client
  // Key by source port since that's what we need to route responses back to
  UdpSession session;
  session.client_ip = src_ip;
  session.client_port = src_port;
  session.last_activity = millis();
  this->sessions_[src_port] = session;

  ESP_LOGD(TAG, "Session created/updated for %s:%d", src_ip_buf, src_port);

  // Forward to STA network as broadcast
  if (this->sta_socket_ == nullptr) {
    ESP_LOGW(TAG, "STA socket not available, cannot forward");
    return;
  }

  // Create broadcast address for STA network
  struct sockaddr_in broadcast_addr;
  memset(&broadcast_addr, 0, sizeof(broadcast_addr));
  broadcast_addr.sin_family = AF_INET;
  broadcast_addr.sin_port = htons(this->port_);
  broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;  // 255.255.255.255

  ssize_t sent = this->sta_socket_->sendto(this->buffer_, len, 0,
                                            (struct sockaddr *) &broadcast_addr, sizeof(broadcast_addr));

  if (sent < 0) {
    ESP_LOGE(TAG, "Failed to forward to STA network: %d (%s)", errno, strerror(errno));
  } else {
    ESP_LOGD(TAG, "Forwarded %d bytes to STA network broadcast", sent);
  }
}

void UdpProxy::process_sta_socket() {
  if (this->sta_socket_ == nullptr || !this->sta_socket_->ready()) {
    return;
  }

  int fd = this->sta_socket_->get_fd();
  if (fd < 0) {
    return;
  }

  struct sockaddr_in sender_addr;
  socklen_t sender_addr_len = sizeof(sender_addr);

  ssize_t len = recvfrom(fd, this->buffer_, sizeof(this->buffer_), MSG_DONTWAIT,
                         (struct sockaddr *) &sender_addr, &sender_addr_len);

  if (len < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      ESP_LOGE(TAG, "STA socket recvfrom failed: %d (%s)", errno, strerror(errno));
    }
    return;
  }

  if (len == 0) {
    return;
  }

  network::IPAddress src_ip;
  src_ip = sender_addr.sin_addr.s_addr;
  uint16_t src_port = ntohs(sender_addr.sin_port);

  char src_ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
  ESP_LOGD(TAG, "Received %d bytes on STA socket from %s:%d", len, src_ip.str_to(src_ip_buf), src_port);

  // Forward response to all active AP clients
  if (this->sessions_.empty()) {
    ESP_LOGV(TAG, "No active sessions, dropping response");
    return;
  }

  if (this->ap_socket_ == nullptr) {
    ESP_LOGW(TAG, "AP socket not available, cannot forward response");
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
    ESP_LOGD(TAG, "Forwarding response to AP client %s:%d",
             session.client_ip.str_to(client_ip_buf), session.client_port);

    ssize_t sent = this->ap_socket_->sendto(this->buffer_, len, 0,
                                             (struct sockaddr *) &client_addr, sizeof(client_addr));

    if (sent < 0) {
      ESP_LOGE(TAG, "Failed to forward to AP client: %d (%s)", errno, strerror(errno));
    } else {
      forwarded_count++;
      // Update session activity
      session.last_activity = millis();
    }
  }

  ESP_LOGD(TAG, "Forwarded response to %d AP client(s)", forwarded_count);
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
