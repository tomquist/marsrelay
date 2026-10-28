#pragma once

#include "esphome/core/component.h"

namespace esphome::disable_ap_pmf {

/// Turns protected management frames (802.11w) off on the access point.
///
/// Some clients, the B2500's WiFi module among them, get PMF wrong: the access
/// point rejects their protected frames, and the SA query that follows times
/// out and drops them. Without PMF there is nothing to get wrong. The station
/// side is untouched.
class DisableApPmf : public Component {
 public:
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
};

}  // namespace esphome::disable_ap_pmf
