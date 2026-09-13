#include "marstack_https.h"

#if defined(USE_ESP32) && defined(USE_ESP_IDF)

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <strings.h>

#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <mbedtls/ssl.h>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "marstack_cert.h"

namespace esphome {
namespace marstack {

static const char *const TAG = "marstack.https";

namespace {

// Request line plus headers. The battery sends a handful of short headers; a
// request that does not fit is not one of ours.
constexpr size_t MAX_HEADER_BYTES = 2048;
// Give up on a client that connects but never completes a request.
constexpr uint32_t REQUEST_TIMEOUT_MS = 15000;
// mbedTLS handshakes need well over 8 KiB of stack on their own.
constexpr uint32_t TASK_STACK_BYTES = 12288;

bool expired(uint32_t deadline) { return static_cast<int32_t>(millis() - deadline) > 0; }

bool is_want_io(ssize_t ret) {
  return ret == ESP_TLS_ERR_SSL_WANT_READ || ret == ESP_TLS_ERR_SSL_WANT_WRITE;
}

/// Case-insensitive search for a header value in a raw header block.
bool header_value(const std::string &headers, const char *name, std::string &out) {
  const size_t name_len = strlen(name);
  for (size_t pos = 0; pos + name_len < headers.size(); pos++) {
    if (pos != 0 && headers[pos - 1] != '\n') {
      continue;
    }
    if (strncasecmp(headers.c_str() + pos, name, name_len) != 0) {
      continue;
    }
    size_t value = pos + name_len;
    if (value >= headers.size() || headers[value] != ':') {
      continue;
    }
    value++;
    while (value < headers.size() && (headers[value] == ' ' || headers[value] == '\t')) {
      value++;
    }
    size_t end = headers.find('\r', value);
    if (end == std::string::npos) {
      end = headers.size();
    }
    out = headers.substr(value, end - value);
    return true;
  }
  return false;
}

}  // namespace

void MarstackHttps::setup() {
  this->request_queue_ = xQueueCreate(1, sizeof(Handoff *));
  this->response_queue_ = xQueueCreate(1, sizeof(Handoff *));
  if (this->request_queue_ == nullptr || this->response_queue_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create handoff queues");
    this->mark_failed();
    return;
  }
  this->start_requested_ = true;
}

void MarstackHttps::loop() {
  // Start once, after the network is up, so bind() has an address to bind to.
  if (this->start_requested_ && this->task_handle_ == nullptr) {
    this->start_requested_ = false;
    const BaseType_t rc =
        xTaskCreate(&MarstackHttps::task_entry_, "marstack_tls", TASK_STACK_BYTES, this, 5, &this->task_handle_);
    if (rc != pdPASS) {
      ESP_LOGE(TAG, "Failed to create TLS task (rc=%d)", (int) rc);
      this->task_handle_ = nullptr;
      this->mark_failed();
      return;
    }
  }

  // Answer whatever the TLS task has parsed. Dispatching here rather than in
  // the task means the `on_request` / `on_venus_upload` automations — which
  // typically publish over MQTT — run on the same task as every other ESPHome
  // automation.
  Handoff *handoff = nullptr;
  while (xQueueReceive(this->request_queue_, &handoff, 0) == pdTRUE) {
    this->parent_->dispatch(*handoff->request, *handoff->response);
    xQueueSend(this->response_queue_, &handoff, portMAX_DELAY);
  }
}

void MarstackHttps::task_entry_(void *param) { static_cast<MarstackHttps *>(param)->task_loop_(); }

bool MarstackHttps::start_listener_() {
  this->listen_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (this->listen_fd_ < 0) {
    ESP_LOGE(TAG, "socket() failed: %s", strerror(errno));
    return false;
  }

  int enable = 1;
  ::setsockopt(this->listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(this->port_);
  if (::bind(this->listen_fd_, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
    ESP_LOGE(TAG, "bind() to port %u failed: %s", this->port_, strerror(errno));
    ::close(this->listen_fd_);
    this->listen_fd_ = -1;
    return false;
  }
  if (::listen(this->listen_fd_, 2) != 0) {
    ESP_LOGE(TAG, "listen() failed: %s", strerror(errno));
    ::close(this->listen_fd_);
    this->listen_fd_ = -1;
    return false;
  }

  ESP_LOGI(TAG, "Serving the Marstek cloud over TLS on port %u", this->port_);
  return true;
}

void MarstackHttps::task_loop_() {
  while (this->listen_fd_ < 0) {
    if (!this->start_listener_()) {
      // Usually just "network not up yet"; retry rather than give up.
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }

  while (true) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(this->listen_fd_, &read_fds);

    struct timeval timeout = {};
    timeout.tv_sec = 0;
    timeout.tv_usec = 250000;
    const int ready = ::select(this->listen_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
    if (ready > 0 && FD_ISSET(this->listen_fd_, &read_fds)) {
      this->accept_connection_();
    } else if (ready < 0 && errno != EINTR) {
      ESP_LOGW(TAG, "select() failed: %s", strerror(errno));
      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    this->reap_held_();
  }
}

void MarstackHttps::accept_connection_() {
  struct sockaddr_in peer_addr = {};
  socklen_t peer_len = sizeof(peer_addr);
  const int fd = ::accept(this->listen_fd_, (struct sockaddr *) &peer_addr, &peer_len);
  if (fd < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ESP_LOGW(TAG, "accept() failed: %s", strerror(errno));
    }
    return;
  }

  char peer_buf[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, &peer_addr.sin_addr, peer_buf, sizeof(peer_buf));

  // Held connections occupy sockets, which are scarce. If they have filled the
  // budget, free the oldest rather than refuse the battery.
  if (this->held_.size() >= static_cast<size_t>(this->max_connections_)) {
    ESP_LOGW(TAG, "Connection budget (%u) reached, ending the oldest held connection early",
             this->max_connections_);
    this->close_session_(this->held_.front().fd, this->held_.front().tls);
    this->held_.erase(this->held_.begin());
  }

  // Serving is synchronous — the handshake alone can take a second or two —
  // so sweep again on the way out rather than leaving held connections to
  // sit past their deadline until the next select() wakes up.
  this->serve_connection_(fd, peer_buf);
  this->reap_held_();
}

void MarstackHttps::serve_connection_(int fd, const std::string &peer) {
  // A receive timeout keeps the blocking handshake and read loops bounded; both
  // treat a timeout as "nothing yet" and check their own deadline.
  struct timeval io_timeout = {};
  io_timeout.tv_sec = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &io_timeout, sizeof(io_timeout));
  io_timeout.tv_sec = 5;
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &io_timeout, sizeof(io_timeout));

  esp_tls_cfg_server_t cfg = {};
  cfg.servercert_buf = reinterpret_cast<const unsigned char *>(MARSTACK_SERVER_CERT_PEM);
  cfg.servercert_bytes = sizeof(MARSTACK_SERVER_CERT_PEM);
  cfg.serverkey_buf = reinterpret_cast<const unsigned char *>(MARSTACK_SERVER_KEY_PEM);
  cfg.serverkey_bytes = sizeof(MARSTACK_SERVER_KEY_PEM);
  cfg.tls_handshake_timeout_ms = 15000;

  esp_tls_t *tls = esp_tls_init();
  if (tls == nullptr) {
    ESP_LOGE(TAG, "esp_tls_init() failed (out of memory?)");
    ::close(fd);
    return;
  }

  const int handshake = esp_tls_server_session_create(&cfg, fd, tls);
  if (handshake != 0) {
    // The most likely cause is a TLS version or cipher suite the battery wants
    // and this build of mbedTLS does not offer.
    ESP_LOGW(TAG, "TLS handshake with %s failed: %d (-0x%04X)", peer.c_str(), handshake,
             handshake < 0 ? -handshake : handshake);
    esp_tls_server_session_delete(tls);
    ::close(fd);
    return;
  }

  Request req;
  req.source_ip = peer;
  if (!this->read_request_(tls, req)) {
    this->close_session_(fd, tls);
    return;
  }

  Response res;
  Handoff handoff{&req, &res};
  Handoff *handoff_ptr = &handoff;
  // The ESPHome main loop fills in the response. Waiting indefinitely is safe:
  // `handoff` lives on this stack until the answer comes back, and a main loop
  // that never runs again means the device is gone anyway.
  xQueueSend(this->request_queue_, &handoff_ptr, portMAX_DELAY);
  xQueueReceive(this->response_queue_, &handoff_ptr, portMAX_DELAY);

  if (res.code == 404 && this->accept_all_) {
    // Opt-in, and off by default: answering an endpoint whose expected reply
    // nobody has reverse-engineered can change what the battery does in ways
    // nobody has tested. Watch the log first, then decide.
    ESP_LOGW(TAG, "accept_all: answering %s %s with code 0", req.method.c_str(), req.url.c_str());
    res.code = 200;
    res.content_type = "application/json";
    res.body = "{\"code\":0,\"message\":\"success\",\"data\":null}";
    res.kong_headers = true;
  }

  if (!this->write_all_(tls, this->parent_->build_raw_response(res))) {
    this->close_session_(fd, tls);
    return;
  }

  this->hold_or_close_(fd, tls);
}

bool MarstackHttps::read_request_(esp_tls_t *tls, Request &req) {
  std::string buffer;
  const uint32_t deadline = millis() + REQUEST_TIMEOUT_MS;

  size_t header_end = std::string::npos;
  char chunk[512];
  while (header_end == std::string::npos) {
    if (expired(deadline)) {
      ESP_LOGW(TAG, "Timed out waiting for a request from %s", req.source_ip.c_str());
      return false;
    }
    const ssize_t read = esp_tls_conn_read(tls, chunk, sizeof(chunk));
    if (is_want_io(read)) {
      continue;
    }
    if (read <= 0) {
      ESP_LOGD(TAG, "Connection from %s closed before a request arrived (%d)", req.source_ip.c_str(), (int) read);
      return false;
    }
    buffer.append(chunk, static_cast<size_t>(read));
    header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos && buffer.size() > MAX_HEADER_BYTES) {
      ESP_LOGW(TAG, "Request headers from %s exceed %zu bytes", req.source_ip.c_str(), MAX_HEADER_BYTES);
      return false;
    }
  }

  const std::string headers = buffer.substr(0, header_end);
  const size_t line_end = headers.find("\r\n");
  const std::string request_line = headers.substr(0, line_end == std::string::npos ? headers.size() : line_end);

  const size_t method_end = request_line.find(' ');
  if (method_end == std::string::npos) {
    ESP_LOGW(TAG, "Malformed request line from %s: %s", req.source_ip.c_str(), request_line.c_str());
    return false;
  }
  const size_t target_end = request_line.find(' ', method_end + 1);
  req.method = request_line.substr(0, method_end);
  std::string target = request_line.substr(
      method_end + 1, (target_end == std::string::npos ? request_line.size() : target_end) - method_end - 1);
  const size_t query_start = target.find('?');
  req.url = query_start == std::string::npos ? target : target.substr(0, query_start);

  size_t content_length = 0;
  std::string value;
  if (header_value(headers, "Content-Length", value)) {
    content_length = static_cast<size_t>(strtoul(value.c_str(), nullptr, 10));
  } else if (header_value(headers, "Transfer-Encoding", value)) {
    // No battery firmware has been seen to do this, and guessing at a decoder
    // for one is worse than saying so in the log. Whatever follows the headers
    // is chunk framing rather than the body, so drop it instead of passing it
    // on as though it were content.
    ESP_LOGW(TAG, "Unsupported Transfer-Encoding '%s' from %s; answering without decoding the body", value.c_str(),
             req.source_ip.c_str());
    req.body_complete = false;
    return true;
  }

  if (content_length > this->max_body_) {
    // Read the excess off the wire without storing it, so the connection stays
    // in sync and the reply is still sent -- but mark the body short, so it is
    // never decoded as if it were the whole record.
    ESP_LOGW(TAG, "Body from %s is %zu bytes, keeping the first %u (max_body)", req.source_ip.c_str(), content_length,
             (unsigned) this->max_body_);
    req.body_complete = false;
  }

  size_t received = buffer.size() - (header_end + 4);
  req.body = buffer.substr(header_end + 4, this->max_body_);
  while (received < content_length) {
    if (expired(deadline)) {
      ESP_LOGW(TAG, "Timed out after %zu of %zu body bytes from %s", received, content_length, req.source_ip.c_str());
      return false;
    }
    const ssize_t read = esp_tls_conn_read(tls, chunk, sizeof(chunk));
    if (is_want_io(read)) {
      continue;
    }
    if (read <= 0) {
      ESP_LOGW(TAG, "Connection from %s closed after %zu of %zu body bytes", req.source_ip.c_str(), received,
               content_length);
      return false;
    }
    if (req.body.size() < this->max_body_) {
      req.body.append(chunk, std::min<size_t>(static_cast<size_t>(read), this->max_body_ - req.body.size()));
    }
    received += static_cast<size_t>(read);
  }
  return true;
}

bool MarstackHttps::write_all_(esp_tls_t *tls, const std::string &data) {
  size_t sent = 0;
  const uint32_t deadline = millis() + REQUEST_TIMEOUT_MS;
  while (sent < data.size()) {
    const ssize_t written = esp_tls_conn_write(tls, data.data() + sent, data.size() - sent);
    if (is_want_io(written)) {
      if (expired(deadline)) {
        ESP_LOGW(TAG, "Timed out sending the response");
        return false;
      }
      continue;
    }
    if (written < 0) {
      ESP_LOGW(TAG, "Response send failed: %d", (int) written);
      return false;
    }
    sent += static_cast<size_t>(written);
  }
  return true;
}

void MarstackHttps::hold_or_close_(int fd, esp_tls_t *tls) {
  if (this->hold_time_ms_ == 0) {
    this->close_session_(fd, tls);
    return;
  }
  this->held_.push_back(HeldConnection{fd, tls, millis() + this->hold_time_ms_});
}

void MarstackHttps::close_session_(int fd, esp_tls_t *tls) {
  if (tls != nullptr) {
    // A clean close_notify, never a reset: it is the firmware's only early exit
    // from its receive loop that keeps the bytes it already has.
    auto *ssl = static_cast<mbedtls_ssl_context *>(esp_tls_get_ssl_context(tls));
    if (ssl != nullptr) {
      mbedtls_ssl_close_notify(ssl);
    }
    // esp_tls_server_session_delete() frees the TLS state but leaves the socket
    // to its owner, which is us — we opened it with accept().
    esp_tls_server_session_delete(tls);
  }
  if (fd >= 0) {
    ::close(fd);
  }
}

void MarstackHttps::reap_held_() {
  const uint32_t now = millis();
  for (auto it = this->held_.begin(); it != this->held_.end();) {
    if (static_cast<int32_t>(now - it->close_at) < 0) {
      ++it;
      continue;
    }
    this->close_session_(it->fd, it->tls);
    it = this->held_.erase(it);
  }
}

void MarstackHttps::dump_config() {
  ESP_LOGCONFIG(TAG, "Marstack HTTPS (Venus telemetry upload):");
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  Hold time: %u ms", (unsigned) this->hold_time_ms_);
  ESP_LOGCONFIG(TAG, "  Max body: %u bytes", (unsigned) this->max_body_);
  ESP_LOGCONFIG(TAG, "  Max connections: %u", this->max_connections_);
  ESP_LOGCONFIG(TAG, "  Accept all paths: %s", YESNO(this->accept_all_));
}

}  // namespace marstack
}  // namespace esphome

#endif
