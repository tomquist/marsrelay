#include "disable_ap_pmf.h"

#include "esphome/core/log.h"
#include "esp_wifi.h"

namespace esphome::disable_ap_pmf {

static const char *const TAG = "disable_ap_pmf";

// Set once the access point has asked for its PMF settings and got "off".
static volatile bool applied = false;

void DisableApPmf::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Access point PMF:\n"
                "  Disabled: %s",
                applied ? "yes" : "not yet (the access point has not started)");
}

}  // namespace esphome::disable_ap_pmf

// hostapd asks for the access point's settings when the access point starts,
// and builds the RSN element it advertises from them. The station's settings
// pass through unchanged.
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" void __real_esp_wifi_get_pmf_config_internal(wifi_pmf_config_t *pmf_cfg, uint8_t ifx);
extern "C" void __wrap_esp_wifi_get_pmf_config_internal(wifi_pmf_config_t *pmf_cfg, uint8_t ifx) {
  __real_esp_wifi_get_pmf_config_internal(pmf_cfg, ifx);
  if (ifx == WIFI_IF_AP) {
    pmf_cfg->capable = false;
    pmf_cfg->required = false;
    esphome::disable_ap_pmf::applied = true;
  }
}
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
