#include "marstack_webserver.h"

#ifdef USE_NETWORK
#include <ctime>
#include <string>

#include "esphome/core/log.h"
#include "esphome/components/network/ip_address.h"

namespace esphome {
namespace marstack_webserver {

static const char *const TAG = "marstack_webserver";

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

std::string homepage_html(const char *hostname) {
  const bool is_eu = hostname != nullptr && std::string(hostname) == "eu.hamedata.com";
  const char *message =
      is_eu ? "Marsrelay is active for eu.hamedata.com traffic."
            : "Marsrelay emulates Marstek cloud APIs and relays data to the local MQTT broker.";

  std::string html;
  html.append("<h1>Marsrelay</h1>");
  html.append("<h3>Local cloud emulation and MQTT relay for Marstek storage</h3>");
  html.append("<hr>");
  html.append("<p><em>");
  html.append(message);
  html.append("</em></p>");
  html.append("<p>Marsrelay hooks between a Marstek Energy Storage and emulates their cloud services ");
  html.append("while relaying everything to the local MQTT broker.</p>");
  return html;
}

std::string formatted_date_string() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
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

bool MarstackWebServer::canHandle(AsyncWebServerRequest *request) const {
  if (request->method() == HTTP_GET) {
    const auto &url = request->url();
    return url == "/" || url == "/prod/api/v1/setB2500Report" || url == "/app/neng/getDateInfoeu.php" ||
           url == "/app/Solar/puterrinfo.php" || url == "/ems/api/v1/getRealtimeSoc";
  }
  if (request->method() == HTTP_POST) {
    return request->url() == "/app/Solar/puterrinfo.php";
  }
  return false;
}

void MarstackWebServer::handleRequest(AsyncWebServerRequest *request) {
  const auto &url = request->url();

  if (request->method() == HTTP_GET && url == "/") {
    const char *host = request->host().c_str();
    request->send(200, "text/html", homepage_html(host).c_str());
    return;
  }

  std::string body = request->arg("plain").c_str();
  std::string method = method_name(request->method());
  std::string source_ip;
#if defined(USE_ARDUINO)
  if (request->client() != nullptr) {
    char ip_buf[network::IP_ADDRESS_BUFFER_SIZE];
    request->client()->remoteIP().str_to(ip_buf);
    source_ip = ip_buf;
  }
#endif
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

}  // namespace marstack_webserver
}  // namespace esphome
#endif
