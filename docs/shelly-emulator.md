# Shelly UDP emulator

Marsrelay can emulate a Shelly Gen2 energy meter (UDP JSON-RPC) directly on the ESP32, fed by ESPHome sensor values — based on the Shelly emulation in [AstraMeter](https://github.com/tomquist/AstraMeter) (formerly b2500-meter). Supports `EM.GetStatus` and `EM1.GetStatus`.

```yaml
sensor:
  - platform: template
    id: grid_power_w
    name: Grid Power
    unit_of_measurement: W

shelly_emulator:
  # Use the port expected by your Marstek/B2500 firmware
  # (e.g. 1010 / 2220 / 2222 / 2223)
  port: 1010
  device_id: marsrelay
  # One sensor (total) or three sensors (a/b/c phases)
  power_sensors:
    - grid_power_w
```
