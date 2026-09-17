#include "marstack.h"

#ifdef USE_NETWORK
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/ip_address.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

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

// Returns the request URL as a std::string in a way that works on both the
// ESP-IDF and Arduino web server backends. On ESP-IDF, AsyncWebServerRequest::url()
// is deprecated (and removed in ESPHome 2026.9.0) in favor of url_to(), which
// writes into a caller-provided buffer; on Arduino the classic url() is used.
std::string request_url(AsyncWebServerRequest *request) {
#if defined(USE_ESP_IDF)
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  return std::string(request->url_to(buffer));
#else
  return std::string(request->url().c_str());
#endif
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

std::string random_hex(size_t bytes) {
  std::vector<uint8_t> buf(bytes);
  if (!random_bytes(buf.data(), buf.size())) {
    for (auto &b : buf) {
      b = static_cast<uint8_t>(random_uint32());
    }
  }
  std::string out;
  out.reserve(bytes * 2);
  static const char *const HEX = "0123456789abcdef";
  for (uint8_t b : buf) {
    out.push_back(HEX[b >> 4]);
    out.push_back(HEX[b & 0x0F]);
  }
  return out;
}

// RFC 1123 date, the way an HTTP `Date:` header spells it. Always GMT.
std::string http_date() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  static const char *const DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char buffer[40];
  snprintf(buffer, sizeof(buffer), "%s, %02d %s %04d %02d:%02d:%02d GMT", DAYS[timeinfo.tm_wday % 7],
           timeinfo.tm_mday, MONTHS[timeinfo.tm_mon % 12], timeinfo.tm_year + 1900, timeinfo.tm_hour, timeinfo.tm_min,
           timeinfo.tm_sec);
  return std::string(buffer);
}

const char *reason_phrase(uint16_t code) {
  switch (code) {
    case 200:
      return "OK";
    case 404:
      return "Not Found";
    default:
      return "OK";
  }
}

int hex_value(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

std::string url_decode(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] == '+') {
      out.push_back(' ');
    } else if (in[i] == '%' && i + 2 < in.size()) {
      const int hi = hex_value(in[i + 1]);
      const int lo = hex_value(in[i + 2]);
      if (hi < 0 || lo < 0) {
        out.push_back(in[i]);
        continue;
      }
      out.push_back(static_cast<char>((hi << 4) | lo));
      i += 2;
    } else {
      out.push_back(in[i]);
    }
  }
  return out;
}

/// Pull one parameter out of an `a=1&b=2` query string, percent-decoded.
bool query_param(const std::string &query, const char *name, std::string &out) {
  const size_t name_len = strlen(name);
  size_t pos = 0;
  while (pos <= query.size()) {
    size_t end = query.find('&', pos);
    if (end == std::string::npos) {
      end = query.size();
    }
    const size_t eq = query.find('=', pos);
    if (eq != std::string::npos && eq < end && eq - pos == name_len &&
        query.compare(pos, name_len, name) == 0) {
      out = url_decode(query.substr(eq + 1, end - eq - 1));
      return true;
    }
    if (end == query.size()) {
      return false;
    }
    pos = end + 1;
  }
  return false;
}

// Telemetry keys whose meaning is confirmed against a second reading of the
// same device (Modbus at the same moment). `scale` is what the raw integer must
// be multiplied by; a scale of 0 means "pass the value through as a string",
// which is what the identifiers and firmware versions need.
struct VenusField {
  const char *key;
  const char *name;
  float scale;
};

constexpr VenusField VENUS_FIELDS[] = {
    {"di", "device_id", 0},
    {"sn", "serial", 0},
    {"ip", "ip", 0},
    {"dt", "device_clock", 0},
    {"wm", "work_mode", 0},  // 10 = 0x0A = RS485 control active
    {"sc", "soc", 1},
    {"pb", "battery_power", 1},
    {"bv", "battery_voltage", 0.01f},
    {"bi", "battery_current", 0.1f},
    {"go", "grid_power", 1},
    {"gv", "grid_voltage", 0.1f},
    {"gf", "grid_frequency", 0.1f},
    {"mc", "max_charge_power", 1},
    {"md", "max_discharge_power", 1},
    {"t1", "temperature_internal", 0.1f},
    {"t2", "temperature_mos", 0.1f},
    {"dn", "control_firmware", 0},
    {"bm", "bms_firmware", 0},
    {"iv", "inverter_firmware", 0},
    {"mv", "mppt_firmware", 0},
};

// Comma-separated lists whose element meaning is confirmed, but not the layout.
constexpr VenusField VENUS_LIST_FIELDS[] = {
    {"tc", "cell_temperatures", 0.1f},
};

const VenusField *lookup_field(const VenusField *fields, size_t count, const std::string &key) {
  for (size_t i = 0; i < count; i++) {
    if (key == fields[i].key) {
      return &fields[i];
    }
  }
  return nullptr;
}

}  // namespace

bool is_data_upload_path(const std::string &url) { return !data_upload_device_id(url).empty(); }

std::string data_upload_device_id(const std::string &url) {
  static const std::string prefix = "/data-upload/v1/venus/";
  if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0) {
    return "";
  }
  if (url.find('/', prefix.size()) != std::string::npos) {
    return "";
  }
  return url.substr(prefix.size());
}

bool is_time_path(const std::string &url) {
  // Firmware has spelled this endpoint several ways ("getDateInfoeu.php",
  // "getDateInfo.php"), and a region suffix may well appear in another, so
  // match the stem rather than an exact name. Anchor it to the last path
  // segment and require an extension after the stem, so an unrelated path such
  // as "/custom/getDateInformation" is left to the rest of the web server.
  const size_t segment = url.rfind('/');
  const std::string name = segment == std::string::npos ? url : url.substr(segment + 1);
  static const std::string stem = "getDateInfo";
  if (name.compare(0, stem.size(), stem) != 0) {
    return false;
  }
  const std::string rest = name.substr(stem.size());
  return rest.empty() || rest.find('.') != std::string::npos;
}

std::string decode_venus_telemetry(const std::string &body) {
  if (body.empty()) {
    return "";
  }

  // The upload carries a single field `d`, holding a URL-encoded query string
  // of roughly seventy short keys. Firmware has sent it both as a JSON object
  // and as a plain form body, so accept either wrapper.
  std::string blob;
  if (body[0] == '{') {
    json::parse_json(body, [&blob](JsonObject root) -> bool {
      if (!root["d"].is<const char *>()) {
        return false;
      }
      blob = root["d"].as<const char *>();
      return true;
    });
  } else {
    query_param(body, "d", blob);
  }
  if (blob.empty()) {
    return "";
  }

  const std::string decoded = url_decode(blob);

  return json::build_json([&decoded](JsonObject root) {
    std::vector<std::pair<std::string, std::string>> unmapped;
    size_t pos = 0;
    while (pos < decoded.size()) {
      size_t end = decoded.find('&', pos);
      if (end == std::string::npos) {
        end = decoded.size();
      }
      const size_t eq = decoded.find('=', pos);
      if (eq == std::string::npos || eq >= end) {
        pos = end + 1;
        continue;
      }
      const std::string key = decoded.substr(pos, eq - pos);
      const std::string value = decoded.substr(eq + 1, end - eq - 1);
      pos = end + 1;

      const auto *field = lookup_field(VENUS_FIELDS, sizeof(VENUS_FIELDS) / sizeof(VENUS_FIELDS[0]), key);
      if (field != nullptr) {
        if (field->scale == 0) {
          root[field->name] = value;
        } else if (field->scale == 1) {
          // Whole units already (watts, percent) — keep them integers rather
          // than round-tripping them through a float.
          root[field->name] = strtol(value.c_str(), nullptr, 10);
        } else {
          root[field->name] = strtol(value.c_str(), nullptr, 10) * field->scale;
        }
        continue;
      }

      const auto *list_field =
          lookup_field(VENUS_LIST_FIELDS, sizeof(VENUS_LIST_FIELDS) / sizeof(VENUS_LIST_FIELDS[0]), key);
      if (list_field != nullptr) {
        JsonArray array = root[list_field->name].to<JsonArray>();
        size_t item = 0;
        while (item < value.size()) {
          size_t item_end = value.find(',', item);
          if (item_end == std::string::npos) {
            item_end = value.size();
          }
          array.add(strtol(value.substr(item, item_end - item).c_str(), nullptr, 10) * list_field->scale);
          item = item_end + 1;
        }
        continue;
      }

      // Everything else is passed through verbatim rather than guessed at.
      unmapped.emplace_back(key, value);
    }

    if (!unmapped.empty()) {
      JsonObject out = root["unmapped"].to<JsonObject>();
      for (const auto &entry : unmapped) {
        out[entry.first] = entry.second;
      }
    }
  });
}

std::string Marstack::formatted_date_string_() const {
  time_t now = time(nullptr);
  struct tm timeinfo;
  // Use local time so the date handed to the battery honors the configured
  // timezone. This relies on the system clock being set by a `time:` platform
  // (e.g. sntp); without one the clock stays at ~1970 and the battery's time
  // ends up out of sync. The real cloud answers in the device's local time too.
  localtime_r(&now, &timeinfo);
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "_%04d_%02d_%02d_%02d_%02d_%02d_", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
           timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return std::string(buffer) + this->time_suffix_;
}

void Marstack::dispatch(const Request &req, Response &res) {
  this->request_count_++;
  this->last_request_ = millis();
  this->has_request_ = true;

  ESP_LOGD(TAG, "%s %s from %s (body %zu bytes)", req.method.c_str(), req.url.c_str(), req.source_ip.c_str(),
           req.body.size());
  // The body is external input: it can carry newlines that forge log lines,
  // and a telemetry upload is kilobytes of it every few minutes. Keep it out of
  // the default log level -- `on_request` already hands it to automations for
  // anyone who wants it.
  if (!req.body.empty()) {
    ESP_LOGV(TAG, "%s %s body: %s", req.method.c_str(), req.url.c_str(), req.body.c_str());
  }

  // A body the transport could not deliver whole never reaches an automation
  // either: a consumer of `on_request` cannot tell a truncated payload from a
  // complete one, so a deceptive partial record is worse than none. The
  // transport has already logged why it came up short.
  static const std::string EMPTY_BODY;
  const std::string &trigger_body = req.body_complete ? req.body : EMPTY_BODY;
  if (!req.body_complete) {
    ESP_LOGD(TAG, "%s %s: body incomplete, handing automations an empty body", req.method.c_str(), req.url.c_str());
  }
  for (auto *trigger : this->request_triggers_) {
    trigger->trigger(req.method, req.url, trigger_body, req.source_ip);
  }

  if (req.method == "GET" && req.url == "/prod/api/v1/setB2500Report") {
    res.code = 200;
    res.content_type = "application/json";
    res.body = "{\"code\":1,\"msg\":\"ok\"}";
    return;
  }

  if (req.method == "GET" && is_time_path(req.url)) {
    res.code = 200;
    // The real endpoint labels the clock reply text/html, not text/plain.
    res.content_type = "text/html; charset=utf-8";
    res.body = this->formatted_date_string_();
    return;
  }

  if (req.url == "/app/Solar/puterrinfo.php" && (req.method == "POST" || req.method == "GET")) {
    res.code = 200;
    res.content_type = "text/plain";
    res.body = req.method == "POST" ? "_1" : "_2";
    return;
  }

  if (req.method == "POST" && is_data_upload_path(req.url)) {
    const std::string device_id = data_upload_device_id(req.url);
    // Counted whether or not the body arrived whole: the point of this
    // endpoint is the acknowledgement, which the battery gets either way.
    this->venus_upload_count_++;
    if (!req.body_complete) {
      // Decoding half a record would publish wrong values under the device's
      // own name, which is worse than publishing none. Answer anyway: the
      // acknowledgement is what keeps the battery from resetting itself, and
      // the real cloud would have accepted this upload too.
      ESP_LOGW(TAG, "Upload from %s was not received whole; answering it but not decoding it", device_id.c_str());
    } else if (!this->venus_upload_triggers_.empty()) {
      const std::string telemetry = decode_venus_telemetry(req.body);
      if (telemetry.empty()) {
        ESP_LOGW(TAG, "Telemetry upload from %s carried no 'd' field", device_id.c_str());
      } else {
        for (auto *trigger : this->venus_upload_triggers_) {
          trigger->trigger(device_id, telemetry);
        }
      }
    }
    // The firmware's own check is strstr() for "code": followed by atoi() on
    // what comes next, so this must evaluate to 0 to count as accepted —
    // anything else leaves the record in the upload buffer, which is what the
    // network-chip reset watchdog counts. The surrounding shape mirrors the
    // real endpoint, which answers e.g. {"code":51,"message":"..."} on reject.
    res.code = 200;
    res.content_type = "application/json";
    res.body = "{\"code\":0,\"message\":\"success\",\"data\":null}";
    res.kong_headers = true;
    return;
  }

  if (req.method == "GET" && req.url == "/ems/api/v1/getRealtimeSoc") {
    res.code = 200;
    res.content_type = "application/json";
    res.body = "{\"code\":1,\"show\":0,\"msg\":\"ok\",\"data\":{\"soc\":0,\"time_no\":0}}";
    return;
  }

  res.code = 404;
  res.content_type = "text/plain";
  res.body = "";
}

std::string Marstack::build_raw_response(const Response &res) const {
  std::string out;
  out.reserve(res.body.size() + 512);
  out.append("HTTP/1.1 ");
  out.append(std::to_string(res.code));
  out.append(" ");
  out.append(reason_phrase(res.code));
  out.append("\r\nDate: ");
  out.append(http_date());
  out.append("\r\n");

  if (res.code != 200) {
    // Headers alone already end in CRLF CRLF, which is what the firmware's
    // receive loop waits for, so an error needs no body.
    out.append("Content-Length: 0\r\nConnection: keep-alive\r\n\r\n");
    return out;
  }

  // Header set, order and framing are copied from a capture of the real cloud.
  // A functionally equivalent reply assembled by a normal HTTP server — same
  // body, same chunked encoding, but with Keep-Alive added and the headers in a
  // different order — was rejected by the battery, which then retried four
  // times and gave up without setting its clock. Do not "simplify" this back
  // into an ordinary response.
  out.append("Content-Type: ");
  out.append(res.content_type != nullptr ? res.content_type : "text/plain");
  out.append("\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\nTrace-Id: ");
  out.append(random_hex(16));
  out.append("\r\n");

  if (res.kong_headers) {
    // The upload host sits behind a Kong API gateway and adds these on top of
    // the headers the clock host sends. Captured from the real endpoint.
    out.append("vary: Origin\r\n");
    out.append("Access-Control-Allow-Credentials: true\r\n");
    out.append("X-Kong-Upstream-Latency: 2\r\n");
    out.append("X-Kong-Proxy-Latency: 0\r\n");
    out.append("Via: 1.1 kong/3.9.1\r\n");
    out.append("X-Kong-Request-Id: ");
    out.append(random_hex(16));
    out.append("\r\n");
    out.append("Strict-Transport-Security: max-age=31536000; includeSubDomains\r\n");
  }

  out.append("\r\n");

  char chunk_header[16];
  snprintf(chunk_header, sizeof(chunk_header), "%x\r\n", static_cast<unsigned>(res.body.size()));
  out.append(chunk_header);
  out.append(res.body);
  out.append("\r\n0\r\n\r\n");
  return out;
}

bool Marstack::canHandle(AsyncWebServerRequest *request) const {
  const std::string url = request_url(request);
  if (request->method() == HTTP_GET) {
    return url == "/prod/api/v1/setB2500Report" || is_time_path(url) || url == "/app/Solar/puterrinfo.php" ||
           url == "/ems/api/v1/getRealtimeSoc";
  }
  if (request->method() == HTTP_POST) {
    return url == "/app/Solar/puterrinfo.php" || is_data_upload_path(url);
  }
  return false;
}

void Marstack::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  // Collect the POST body as the web server delivers it. This is the
  // AsyncWebHandler hook; it used to be spelled onBody(), which overrode
  // nothing, so bodies never reached handleRequest() and the telemetry a
  // pre-v150 Venus uploads in the clear was logged as empty.
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
  ESP_LOGV(TAG, "handleBody: index=%zu, len=%zu, total=%zu, buffer_size=%zu", index, len, total,
           this->post_body_buffer_.length());
}

void Marstack::handleRequest(AsyncWebServerRequest *request) {
  Request req;
  req.url = request_url(request);
  req.method = method_name(request->method());

  // For POST requests, read the body from collected buffer or "plain" parameter
  // For GET requests, body will be empty
  if (request->method() == HTTP_POST) {
    // First, try to use body collected via onBody callback
    if (request == this->current_post_request_ && !this->post_body_buffer_.empty()) {
      req.body = this->post_body_buffer_;
      // Clear buffer for next request
      this->post_body_buffer_.clear();
      this->current_post_request_ = nullptr;
    } else {
      // Fallback: try to get POST body from arg("plain")
      auto body_param = request->arg("plain");
      if (body_param.length() > 0) {
        req.body.assign(body_param.c_str(), body_param.length());
      } else {
        // Try to get body from "plain" parameter directly as fallback
        auto *plain_param = request->getParam("plain");
        if (plain_param != nullptr) {
          req.body.assign(plain_param->value().c_str(), plain_param->value().length());
        }
      }
    }
  }

#if defined(USE_ARDUINO)
  if (request->client() != nullptr) {
    char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
    request->client()->remoteIP().str_to(ip_buf);
    req.source_ip = ip_buf;
  }
#elif defined(USE_ESP_IDF)
  // Get client IP from ESP-IDF httpd_req_t
  // AsyncWebServerRequest has operator httpd_req_t*() conversion operator
  httpd_req_t *idf_req = static_cast<httpd_req_t *>(*request);
  if (idf_req != nullptr) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    const int sockfd = httpd_req_to_sockfd(idf_req);
    if (sockfd >= 0 && getpeername(sockfd, (struct sockaddr *) &client_addr, &addr_len) == 0) {
      const char *ip_str = inet_ntoa(client_addr.sin_addr);
      if (ip_str != nullptr) {
        req.source_ip = ip_str;
      }
    }
  }
#endif

  log_query_params(request, req.url.c_str());

  Response res;
  this->dispatch(req, res);

  if (res.code == 404) {
    // Leave unmatched paths to the rest of the ESPHome web server rather than
    // claiming them; canHandle() only routes the cloud endpoints here anyway.
    request->send(404, "text/plain", "Not found");
    return;
  }

#if defined(USE_ESP_IDF)
  if (this->raw_responses_) {
    const std::string raw = this->build_raw_response(res);
    httpd_req_t *idf_req = static_cast<httpd_req_t *>(*request);
    const int sockfd = idf_req != nullptr ? httpd_req_to_sockfd(idf_req) : -1;
    if (sockfd >= 0) {
      size_t sent = 0;
      bool ok = true;
      while (sent < raw.size()) {
        const int written = httpd_socket_send(idf_req->handle, sockfd, raw.data() + sent, raw.size() - sent, 0);
        if (written < 0) {
          ESP_LOGW(TAG, "Raw response send failed: %d", written);
          ok = false;
          break;
        }
        sent += static_cast<size_t>(written);
      }
      if (ok) {
        // The real cloud answers keep-alive and does not close; the web server
        // keeps the socket and reuses it for the battery's next request.
        return;
      }
    }
    ESP_LOGW(TAG, "Falling back to the framework's response framing");
  }
#endif

  request->send(res.code, res.content_type != nullptr ? res.content_type : "text/plain", res.body.c_str());
}

void Marstack::dump_config() {
  ESP_LOGCONFIG(TAG, "Marstack cloud emulation:");
  ESP_LOGCONFIG(TAG, "  Raw cloud response framing: %s", YESNO(this->raw_responses_));
  ESP_LOGCONFIG(TAG, "  Clock reply suffix: %s", this->time_suffix_.c_str());
}

}  // namespace marstack
}  // namespace esphome
#endif
