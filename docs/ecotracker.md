# EcoTracker

A battery on the Marsrelay access point can't reach an everHome EcoTracker on your home network. The battery polls the EcoTracker over HTTP, which is TCP, and the [UDP proxy](../README.md#how-it-works) only bridges the UDP broadcasts of Shelly and CT meters ([issue #16](https://github.com/tomquist/marsrelay/issues/16)).

Marsrelay can read the EcoTracker itself instead, over its home WiFi connection, and pass the reading to the battery with the [Shelly emulator](shelly-emulator.md). The battery then treats Marsrelay as a Shelly Pro 3EM.

## 1. Check the EcoTracker

Open `http://<ecotracker-ip>/v1/json` in a browser. You should see a reply with a `power` value. If you don't, turn on the local HTTP server in the EcoTracker's settings.

Give the EcoTracker a fixed IP address in your router, so the address in the config keeps working.

## 2. Add it to the Marsrelay config

In `marsrelay_esp32s3.yaml`, add the EcoTracker's address to the `substitutions:` block:

```yaml
substitutions:
  # ...
  ecotracker_host: 192.168.1.50
```

Then remove the `udp_proxy:` entry that uses `${udp_proxy_port}`, because the Shelly emulator answers on that port now. Add this block in its place:

```yaml
http_request:
  # Marsrelay waits for each request before doing anything else, so keep
  # this short. An EcoTracker on the home network answers well within it.
  timeout: 1s

sensor:
  - platform: template
    id: grid_power_w
    name: Grid Power
    unit_of_measurement: W
    device_class: power
    state_class: measurement

interval:
  - interval: 1s
    then:
      - http_request.get:
          url: http://${ecotracker_host}/v1/json
          capture_response: true
          max_response_buffer_size: 1024
          on_response:
            then:
              - lambda: |-
                  float power = NAN;
                  if (response->status_code == 200) {
                    json::parse_json(body, [&](JsonObject root) -> bool {
                      if (!root["power"].is<float>()) return false;
                      power = root["power"].as<float>();
                      return true;
                    });
                  }
                  id(grid_power_w).publish_state(power);
          on_error:
            - lambda: id(grid_power_w).publish_state(NAN);

shelly_emulator:
  port: ${udp_proxy_port}
  power_sensors:
    - grid_power_w
```

Set `udp_proxy_port` to the Shelly Pro 3EM port your battery uses: `1010` for Venus, Jupiter and B2500 firmware up to v224, or `2220` for B2500 firmware v226 and later.

Rebuild and flash. **Grid Power** then shows the EcoTracker's reading in Home Assistant as well.

## 3. Switch the battery to Shelly

In the Marstek app, change the battery's meter from EcoTracker to **Shelly Pro 3EM**. The battery finds Marsrelay on the access point the same way it finds a real Shelly.

## If the EcoTracker stops answering

The reading goes to unknown and the battery is sent 0 W, so it stops adjusting to an old value. Normal control resumes with the next good reading. Each failed request holds Marsrelay up for the 1 s timeout, so fix an unreachable EcoTracker rather than leaving it.
