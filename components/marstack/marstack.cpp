#include "marstack.h"

#ifdef USE_NETWORK
#include <ctime>
#include <string>

#include "esphome/core/log.h"
#include "esphome/components/network/ip_address.h"

#if defined(USE_ESP_IDF)
#include "esp_http_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <cstring>
#endif

namespace esphome {
namespace marstack {

static const char *const TAG = "marstack";

namespace {

void log_query_params(AsyncWebServerRequest *request, const char *path) {
#if defined(USE_ARDUINO)
  if (request->params() == 0) {
    return;
  }
  std::string query;
  for (size_t i = 0; i < request->params(); i++) {
    auto *param = request->getParam(i);
    if (param == nullptr || param->isFile()) {
      continue;
    }
    if (!query.empty()) {
      query.append("&");
    }
    query.append(param->name().c_str());
    query.append("=");
    query.append(param->value().c_str());
  }
  if (!query.empty()) {
    ESP_LOGD(TAG, "%s query params: %s", path, query.c_str());
  }
#else
  (void) request;
  (void) path;
#endif
}

std::string formatted_date_string() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  // Use local time so the date handed to the battery honors the configured
  // timezone. This relies on the system clock being set by a `time:` platform
  // (e.g. sntp); without one the clock stays at ~1970 and the battery's time
  // ends up out of sync.
  localtime_r(&now, &timeinfo);
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "_%04d_%02d_%02d_%02d_%02d_%02d_04_0_0_0", timeinfo.tm_year + 1900,
           timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return std::string(buffer);
}

const char *method_name(http_method method) {
  switch (method) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    case HTTP_PUT:
      return "PUT";
    case HTTP_PATCH:
      return "PATCH";
    case HTTP_DELETE:
      return "DELETE";
    default:
      return "UNKNOWN";
  }
}

}  // namespace

bool Marstack::canHandle(AsyncWebServerRequest *request) const {
  if (request->method() == HTTP_GET) {
    const auto &url = request->url();
    return url == "/prod/api/v1/setB2500Report" || url == "/app/neng/getDateInfoeu.php" ||
           url == "/app/Solar/puterrinfo.php" || url == "/ems/api/v1/getRealtimeSoc";
  }
  if (request->method() == HTTP_POST) {
    return request->url() == "/app/Solar/puterrinfo.php";
  }
  return false;
}

void Marstack::onBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  // Collect POST body data as it arrives
  // This is called by ESPHome's web server when POST body data is received
  if (index == 0) {
    // First chunk - clear buffer and reserve space
    this->post_body_buffer_.clear();
    if (total > 0) {
      this->post_body_buffer_.reserve(total);
    }
    this->current_post_request_ = request;
  }
  // Append data chunk to buffer
  if (data != nullptr && len > 0) {
    this->post_body_buffer_.append(reinterpret_cast<const char *>(data), len);
  }
  ESP_LOGD(TAG, "onBody: index=%zu, len=%zu, total=%zu, buffer_size=%zu", index, len, total, this->post_body_buffer_.length());
}

void Marstack::handleRequest(AsyncWebServerRequest *request) {
  const auto &url = request->url();

  std::string body;
  // For POST requests, read the body from collected buffer or "plain" parameter
  // For GET requests, body will be empty
  if (request->method() == HTTP_POST) {
    // First, try to use body collected via onBody callback
    if (request == this->current_post_request_ && !this->post_body_buffer_.empty()) {
      body = this->post_body_buffer_;
      ESP_LOGD(TAG, "Using POST body from onBody buffer: length=%zu", body.length());
      // Clear buffer for next request
      this->post_body_buffer_.clear();
      this->current_post_request_ = nullptr;
    } else {
      // Fallback: try to get POST body from arg("plain")
      auto body_param = request->arg("plain");
      if (body_param.length() > 0) {
        body.assign(body_param.c_str(), body_param.length());
        ESP_LOGD(TAG, "Extracted POST body from arg: length=%zu, contentLength=%zu", body.length(), request->contentLength());
      } else {
        // Try to get body from "plain" parameter directly as fallback
        auto *plain_param = request->getParam("plain");
        if (plain_param != nullptr) {
          body.assign(plain_param->value().c_str(), plain_param->value().length());
          ESP_LOGD(TAG, "Found body in plain param: length=%zu", body.length());
        } else {
          ESP_LOGD(TAG, "POST body is empty (contentLength=%zu, buffer_empty=%s)", 
                   request->contentLength(), this->post_body_buffer_.empty() ? "yes" : "no");
        }
      }
    }
  }
  std::string method = method_name(request->method());
  std::string source_ip;
#if defined(USE_ARDUINO)
  if (request->client() != nullptr) {
    char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
    request->client()->remoteIP().str_to(ip_buf);
    source_ip = ip_buf;
  }
#elif defined(USE_ESP_IDF)
  // Get client IP from ESP-IDF httpd_req_t
  // AsyncWebServerRequest has operator httpd_req_t*() conversion operator
  httpd_req_t *req = static_cast<httpd_req_t*>(*request);
  if (req != nullptr) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    // Get the socket file descriptor from the request
    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd >= 0) {
      // Get peer address (client IP)
      if (getpeername(sockfd, (struct sockaddr *)&client_addr, &addr_len) == 0) {
        const char *ip_str = inet_ntoa(client_addr.sin_addr);
        if (ip_str != nullptr) {
          source_ip = ip_str;
          ESP_LOGD(TAG, "Client IP: %s", source_ip.c_str());
        } else {
          ESP_LOGW(TAG, "Failed to convert client IP address to string");
        }
      } else {
        ESP_LOGW(TAG, "getpeername failed: %s", strerror(errno));
      }
    } else {
      ESP_LOGW(TAG, "httpd_req_to_sockfd failed");
    }
  } else {
    ESP_LOGW(TAG, "Failed to get httpd_req_t from request");
  }
#endif
  // Log body before triggering to debug MQTT forwarding
  ESP_LOGD(TAG, "Triggering request: method=%s, url=%s, body_length=%zu, body_preview=%.100s", 
           method.c_str(), url.c_str(), body.length(), body.c_str());
  for (auto *trigger : this->request_triggers_) {
    trigger->trigger(method, url.c_str(), body, source_ip);
  }

  if (request->method() == HTTP_GET && url == "/prod/api/v1/setB2500Report") {
    log_query_params(request, "GET /prod/api/v1/setB2500Report");
    request->send(200, "application/json", "{\"code\":1,\"msg\":\"ok\"}");
    return;
  }

  if (request->method() == HTTP_GET && url == "/app/neng/getDateInfoeu.php") {
    log_query_params(request, "GET /app/neng/getDateInfoeu.php");
    request->send(200, "text/plain", formatted_date_string().c_str());
    return;
  }

  if (url == "/app/Solar/puterrinfo.php") {
    if (request->method() == HTTP_POST) {
      if (!body.empty()) {
        ESP_LOGD(TAG, "POST /app/Solar/puterrinfo.php body: %s", body.c_str());
      }
      request->send(200, "text/plain", "_1");
      return;
    }

    if (request->method() == HTTP_GET) {
      if (!body.empty()) {
        ESP_LOGD(TAG, "GET /app/Solar/puterrinfo.php body: %s", body.c_str());
      }
      request->send(200, "text/plain", "_2");
      return;
    }
  }

  if (request->method() == HTTP_GET && url == "/ems/api/v1/getRealtimeSoc") {
    log_query_params(request, "GET /ems/api/v1/getRealtimeSoc");
    request->send(200, "application/json",
                  "{\"code\":1,\"show\":0,\"msg\":\"ok\",\"data\":{\"soc\":0,\"time_no\":0}}");
    return;
  }

  request->send(404, "text/plain", "Not found");
}

}  // namespace marstack
}  // namespace esphome
#endif
