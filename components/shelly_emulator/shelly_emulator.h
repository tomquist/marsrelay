#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#ifdef USE_SHELLY_EMULATOR

#include "esphome/components/socket/socket.h"

namespace esphome {
namespace shelly_emulator {

class ShellyEmulator : public Component {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override;
  void dump_config() override;

  void set_port(uint16_t port) { this->port_ = port; }
  void set_device_id(const std::string &device_id) { this->device_id_ = device_id; }
  void add_power_sensor(sensor::Sensor *sensor) { this->power_sensors_.push_back(sensor); }

 protected:
  void start_();
  void process_socket_();

  float calculate_derived_value_(float power) const;
  void fill_powers_(std::vector<float> &powers) const;

  std::string device_id_{"marsrelay"};
  uint16_t port_{0};

  std::vector<sensor::Sensor *> power_sensors_;

  std::unique_ptr<socket::Socket> socket_;
  bool active_{false};

  uint8_t buffer_[1024]{};
};

}  // namespace shelly_emulator
}  // namespace esphome

#endif  // USE_SHELLY_EMULATOR
