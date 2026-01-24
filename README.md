# Marsrelay

Marsrelay is a simple device that lets you use your Marstek Energy Storage system completely offline while still integrating it with your home automation setup.

## What is Marsrelay?

Marsrelay replaces the Marstek cloud service with a small device that runs locally in your home. Instead of your Marstek battery system connecting to the internet, it connects to Marsrelay, which then connects to your home automation system.

## Why Use Marsrelay?

- **Works Offline**: Your Marstek system doesn't need internet access
- **Privacy**: All data stays in your home network
- **Home Automation Integration**: Connect your Marstek system to home automation projects like [hm2mqtt](https://github.com/tomquist/hm2mqtt)
- **No Cloud Dependencies**: No need to rely on Marstek's cloud services
- **No Device Hacking Required**: The best part? You don't need to modify or hack your Marstek Energy Storage device at all. The only thing you need to do is configure your device to connect to Marsrelay's WiFi access point instead of your regular WiFi network.

## How It Works

When you set up Marsrelay:

1. **Creates a WiFi Network**: Marsrelay opens its own WiFi access point that your Marstek Energy Storage device connects to (just like connecting to WiFi)
2. **Redirects Internet Requests**: When your Marstek device tries to reach the internet, Marsrelay intercepts those requests and redirects them to itself instead
3. **Acts Like Marstek's Server**: Marsrelay includes a web server that responds to your Marstek device the same way Marstek's cloud service would, so your device thinks it's connected normally
4. **Forwards Messages**: Marsrelay includes an MQTT broker (a messaging system) that:
   - Receives messages from your Marstek device
   - Forwards them to your home automation system's MQTT broker
   - This allows projects like hm2mqtt to monitor and control your Marstek system

## The Result

Your Marstek Energy Storage system operates completely offline, thinking it's connected to Marstek's cloud, while all its data and controls are available to your home automation system through MQTT.

---

## Getting Started

### Prerequisites

- ESPHome installed (e.g., via the ESPHome addon in Home Assistant)
- An MQTT broker running on your network
- A tool like [MQTT Explorer](https://github.com/thomasnordquist/MQTT-Explorer) to monitor MQTT messages

### Step 0: Get an ESP32-S3 Board

You'll need an ESP32-S3 development board. You can purchase one here:

- Single board: [Amazon](https://amzn.to/429OJDX) with "octal" PSRAM
- 3-pack (better value): [Amazon](https://amzn.to/3PwGRVv)
- 3-pack mini: [Amazon](https://amzn.to/4qIjp8P) with "quad" PSRAM

### Step 1: Configure ESPHome

Copy the following ESPHome configuration and save it as `marsrelay_esp32s3.yaml`.

```yaml
substitutions:
  device_name: marsrelay-esp32s3
  friendly_name: Marsrelay ESP32S3
  wifi_ssid: "your-home-wifi-ssid"
  wifi_password: "your-home-wifi-password"
  ap_ssid: "marsrelay"
  ap_password: "marsrelay"
  mqtt_broker: 192.168.1.100
  mqtt_username: ""
  mqtt_password: ""
  mqtt_topic_prefix: "marsrelay"
  # UDP proxy port for power meter emulation (see https://github.com/tomquist/b2500-meter)
  # - Port 1010: Shelly Pro 3EM for B2500 firmware up to v224, Jupiter, Venus
  # - Port 2220: Shelly Pro 3EM for B2500 firmware v226+
  udp_proxy_port: "1010"
  psram:
    # Set to true if your ESP32S3 has PSRAM.
    enabled: true
    # This needs to be set correctly! See https://esphome.io/components/psram/ for more information.
    mode: quad

esphome:
  name: ${device_name}
  friendly_name: ${friendly_name}

psram:
  mode: ${psram.mode}
  disabled: ${false if psram.enabled else true}

esp32:
  board: esp32-s3-devkitc-1
  variant: esp32s3
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_LWIP_IPV6: y
      CONFIG_ESP_TLS_INSECURE: y  # Allow skipping certificate verification
      CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY: y  # Skip server certificate verification
      CONFIG_LWIP_MAX_SOCKETS: "58"  # Reduced from 64 to ensure LWIP_SOCKET_OFFSET >= 6 (FD_SETSIZE=64, so offset = 64-58=6)
      CONFIG_LWIP_TCP_MSL: "30"  # Reduce TIME_WAIT timeout from default 60s to 30s (in seconds)
      CONFIG_LWIP_SOCKET_OFFSET: "6"  # Required for esp_vfs_console compatibility (must be >= 6 for esp_vfs_console)

external_components:
  - source:
      type: git
      url: https://github.com/tomquist/marsrelay
      ref: main

logger:

wifi:
  ssid: ${wifi_ssid}
  password: ${wifi_password}
  ap:
    ssid: ${ap_ssid}
    password: ${ap_password}
  use_psram: ${psram.enabled}

capture_dns:
  id: capture_dns_server

# UDP proxy bridges broadcasts between AP and STA networks for power meter discovery
# Add multiple entries if you need to support different firmware versions
udp_proxy:
  - port: ${udp_proxy_port}

mqtt:
  id: mqtt_client
  broker: ${mqtt_broker}
  username: ${mqtt_username}
  password: ${mqtt_password}
  on_connect:
    - lambda: |-
        id(mqtt_client).subscribe("marstek_energy/+/App/+/ctrl", [=](const std::string &topic, const std::string &payload) {
          ESP_LOGD("MQTT", "Received control message: %s", topic.c_str());
          if (topic.find("/App/") == std::string::npos) {
            ESP_LOGD("MQTT", "Not a control message, skipping");
            return;
          }
          if (topic.find("/ctrl") == std::string::npos) {
            ESP_LOGD("MQTT", "Not a control message, skipping");
            return;
          }
          id(local_broker).publish_message(topic, payload);
        });

mosquitto_broker:
  id: local_broker
  tls: true
  tls_skip_verification: true
  port: 8883
  max_clients: 5
  on_message:
    # Only forward messages containing /device/
    - if:
        condition:
          lambda: 'return topic.find("/device/") != std::string::npos;'
        then:
          - mqtt.publish:
              topic: !lambda |-
                return topic;
              payload: !lambda |-
                return payload;

web_server:
  port: 80
  version: 3

marstack:
  id: marstack_http
  on_request:
    - logger.log:
        format: "HTTP %s %s body=%s"
        args: [method.c_str(), url.c_str(), body.c_str()]
    - mqtt.publish:
        topic: ${mqtt_topic_prefix}/request
        payload: !lambda |-
          return json::build_json([&](JsonObject root) {
            root["source_ip"] = source_ip;
            root["url"] = url;
            root["method"] = method;
            root["body"] = body;
          });
```

### Step 2: Adjust Configuration Values

Edit the `substitutions` section at the top of the configuration file:

- **`wifi_ssid`** and **`wifi_password`**: Your home WiFi network credentials (so Marsrelay can connect to your network)
- **`ap_ssid`** and **`ap_password`**: The WiFi network name and password that your Marstek device will connect to (default is "marsrelay" / "marsrelay")
- **`mqtt_broker`**: The IP address of your MQTT broker (e.g., `192.168.1.100`)
- **`mqtt_username`** and **`mqtt_password`**: Your MQTT broker credentials (leave empty if not required)
- **`psram.enabled`** and **`psram.mode`**: Set according to your ESP32-S3 board specifications

### Step 3: Build and Flash

1. Open ESPHome (e.g., via the Home Assistant ESPHome addon)
2. Upload the configuration file
3. Build the firmware image
4. Flash the image to your ESP32-S3 device

### Step 4: Configure Your Marstek Energy Storage Device

1. On your Marstek Energy Storage device, go to the WiFi/Network settings
2. Configure it to connect to the access point named "marsrelay" (or whatever you set as `ap_ssid`)
3. Enter the password you configured (default is "marsrelay")
4. Save the configuration

**That's it!** No hacking, no firmware modifications, no complicated setup. Just change the WiFi network your Marstek device connects to.

### Step 5: Find Your Device Information

1. Open MQTT Explorer (or another MQTT client) and connect to your MQTT broker
2. Wait up to 20 minutes for a message to appear on the topic: `marstek_energy/<deviceType>/device/<deviceId>/ctrl`
3. Note down the **`deviceType`** and **`deviceId`** from the topic

### Step 6: Configure hm2mqtt

Copy the `deviceType` and `deviceId` you found into your [hm2mqtt](https://github.com/tomquist/hm2mqtt) configuration file. Your Marstek Energy Storage system is now integrated with your home automation!

---

## Technical Details

This repository contains ESPHome external components for building Marsrelay:

### Components

- **`mosquitto_broker`**: An embedded MQTT broker that forwards messages from Marstek devices to your main MQTT broker
- **`marstack`**: A web server component that implements Marstek's API endpoints
- **`capture_dns`**: DNS redirection component that intercepts DNS queries and redirects them to the device
- **`udp_proxy`**: UDP proxy component that bridges UDP broadcasts between the AP network (where Marstek devices connect) and the STA network (your home network). This enables zero feed-in control by forwarding UDP discovery/control packets between networks. Supports multiple ports.
- **`wifi`**: Patched WiFi component to support simultaneous access point while station mode is enabled

### Requirements

- ESP32-S3 device (ESP-IDF framework)
- ESPHome
- MQTT broker on your network

## License

MIT License

Copyright (c) 2024

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
