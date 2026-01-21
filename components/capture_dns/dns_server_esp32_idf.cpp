#include "dns_server_esp32_idf.h"
#ifdef USE_ESP32

#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/network/ip_address.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <cstring>

namespace esphome::capture_dns {

static const char *const TAG = "capture_dns.dns";

static constexpr uint16_t DNS_PORT = 53;
static constexpr uint16_t DNS_QR_FLAG = 1 << 15;
static constexpr uint16_t DNS_OPCODE_MASK = 0x7800;
static constexpr uint16_t DNS_QTYPE_A = 0x0001;
static constexpr uint16_t DNS_QCLASS_IN = 0x0001;
static constexpr uint16_t DNS_ANSWER_TTL = 300;

struct DNSHeader {
  uint16_t id;
  uint16_t flags;
  uint16_t qd_count;
  uint16_t an_count;
  uint16_t ns_count;
  uint16_t ar_count;
} __attribute__((packed));

struct DNSQuestion {
  uint16_t type;
  uint16_t dns_class;
} __attribute__((packed));

struct DNSAnswer {
  uint16_t ptr_offset;
  uint16_t type;
  uint16_t dns_class;
  uint32_t ttl;
  uint16_t addr_len;
  uint32_t ip_addr;
} __attribute__((packed));

void DNSServer::start(const network::IPAddress &ip) {
  this->server_ip_ = ip;
  char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
  ESP_LOGI(TAG, "Starting DNS server, will respond with IP: %s", ip.str_to(ip_buf));

  this->socket_ = socket::socket_ip_loop_monitored(SOCK_DGRAM, IPPROTO_UDP);
  if (this->socket_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create UDP socket for DNS server");
    return;
  }

  int enable = 1;
  this->socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  struct sockaddr_storage server_addr = {};
  socklen_t addr_len = socket::set_sockaddr_any((struct sockaddr *) &server_addr, sizeof(server_addr), DNS_PORT);

  int err = this->socket_->bind((struct sockaddr *) &server_addr, addr_len);
  if (err != 0) {
    ESP_LOGE(TAG, "Failed to bind DNS server to port %d: %d (%s)", DNS_PORT, errno, strerror(errno));
    this->socket_ = nullptr;
    return;
  }
  ESP_LOGI(TAG, "DNS server bound to port %d", DNS_PORT);
}

void DNSServer::stop() {
  if (this->socket_ != nullptr) {
    this->socket_->close();
    this->socket_ = nullptr;
    ESP_LOGI(TAG, "DNS server stopped and socket closed");
  } else {
    ESP_LOGV(TAG, "DNS server already stopped");
  }
}

void DNSServer::process_next_request() {
  if (this->socket_ == nullptr || !this->socket_->ready()) {
    return;
  }
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  int fd = this->socket_->get_fd();
  if (fd < 0) {
    return;
  }

  ssize_t len = recvfrom(fd, this->buffer_, sizeof(this->buffer_), MSG_DONTWAIT, (struct sockaddr *) &client_addr,
                         &client_addr_len);

  if (len < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      ESP_LOGE(TAG, "recvfrom failed: %d (%s)", errno, strerror(errno));
    }
    return;
  }

  const char *client_ip = inet_ntoa(client_addr.sin_addr);
  uint16_t client_port = ntohs(client_addr.sin_port);
  ESP_LOGD(TAG, "Received DNS query: %d bytes from %s:%d", len, client_ip, client_port);

  if (len < static_cast<ssize_t>(sizeof(DNSHeader) + 1)) {
    ESP_LOGW(TAG, "DNS request too short (%d bytes), minimum %zu bytes required", len, sizeof(DNSHeader) + 1);
    return;
  }

  DNSHeader *header = (DNSHeader *) this->buffer_;
  uint16_t flags = ntohs(header->flags);
  uint16_t qd_count = ntohs(header->qd_count);
  uint16_t query_id = ntohs(header->id);

  if ((flags & DNS_QR_FLAG) || (flags & DNS_OPCODE_MASK) || qd_count != 1) {
    ESP_LOGW(TAG, "Invalid DNS query: id=0x%04X, flags=0x%04X, qd_count=%d (expected 1)", query_id, flags, qd_count);
    return;
  }

  uint8_t *ptr = this->buffer_ + sizeof(DNSHeader);
  uint8_t *end = this->buffer_ + len;
  uint8_t *domain_start = ptr;
  char domain_name[256] = {0};
  size_t domain_pos = 0;

  // Parse domain name and build string representation
  while (ptr < end && *ptr != 0) {
    uint8_t label_len = *ptr;
    if (label_len > 63) {
      ESP_LOGW(TAG, "Invalid label length %d in domain name", label_len);
      return;
    }
    if (ptr + label_len + 1 > end) {
      ESP_LOGW(TAG, "Domain name extends beyond packet boundary");
      return;
    }
    if (domain_pos > 0 && domain_pos < sizeof(domain_name) - 1) {
      domain_name[domain_pos++] = '.';
    }
    for (uint8_t i = 0; i < label_len && domain_pos < sizeof(domain_name) - 1; i++) {
      domain_name[domain_pos++] = ptr[i + 1];
    }
    ptr += label_len + 1;
  }

  if (ptr >= end || *ptr != 0) {
    ESP_LOGW(TAG, "Domain name not properly terminated");
    return;
  }
  ptr++;
  
  if (domain_pos > 0) {
    ESP_LOGD(TAG, "DNS query [id=0x%04X] for domain: %s", query_id, domain_name);
  } else {
    ESP_LOGD(TAG, "DNS query [id=0x%04X] for root domain", query_id);
  }

  if (ptr + sizeof(DNSQuestion) > end) {
    ESP_LOGW(TAG, "DNS question extends beyond packet boundary");
    return;
  }

  DNSQuestion *question = (DNSQuestion *) ptr;
  uint16_t qtype = ntohs(question->type);
  uint16_t qclass = ntohs(question->dns_class);

  if (qtype != DNS_QTYPE_A || qclass != DNS_QCLASS_IN) {
    ESP_LOGD(TAG, "DNS query type not A/IN: type=0x%04X, class=0x%04X (expected A=0x%04X, IN=0x%04X)", 
             qtype, qclass, DNS_QTYPE_A, DNS_QCLASS_IN);
    return;
  }

  header->flags = htons(DNS_QR_FLAG | 0x8000);
  header->an_count = htons(1);

  size_t question_len = (ptr + sizeof(DNSQuestion)) - this->buffer_ - sizeof(DNSHeader);
  size_t answer_offset = sizeof(DNSHeader) + question_len;

  if (answer_offset + sizeof(DNSAnswer) > sizeof(this->buffer_)) {
    ESP_LOGE(TAG, "DNS response too large: %zu bytes (max %zu)", answer_offset + sizeof(DNSAnswer), sizeof(this->buffer_));
    return;
  }

  DNSAnswer *answer = (DNSAnswer *) (this->buffer_ + answer_offset);

  answer->ptr_offset = htons(0xC000 | sizeof(DNSHeader));
  answer->type = htons(DNS_QTYPE_A);
  answer->dns_class = htons(DNS_QCLASS_IN);
  answer->ttl = htonl(DNS_ANSWER_TTL);
  answer->addr_len = htons(4);

  ip4_addr_t addr = this->server_ip_;
  answer->ip_addr = addr.addr;

  size_t response_len = answer_offset + sizeof(DNSAnswer);

  char response_ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
  const char *response_ip_str = this->server_ip_.str_to(response_ip_buf);
  
  ssize_t sent =
      this->socket_->sendto(this->buffer_, response_len, 0, (struct sockaddr *) &client_addr, client_addr_len);
  if (sent < 0) {
    ESP_LOGE(TAG, "Failed to send DNS response to %s:%d: %d (%s)", 
             inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), errno, strerror(errno));
  } else {
    ESP_LOGD(TAG, "Sent DNS response [id=0x%04X]: %zd bytes, answered %s with %s (TTL=%d)", 
             query_id, sent, domain_pos > 0 ? domain_name : "<root>", response_ip_str, DNS_ANSWER_TTL);
  }
}

}  // namespace esphome::capture_dns

#endif  // USE_ESP32
