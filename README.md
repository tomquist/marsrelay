# Marsrelay Mosquitto Broker External Component

This repository contains a custom ESPHome external component that embeds the Espressif Mosquitto
broker component (`espressif/mosquitto` v2.0.20). It provides:

- An on-message trigger (`on_message`) to react to inbound MQTT messages.
- A `mosquitto_broker.publish_message` automation action to publish messages to connected clients.

## Usage

```yaml
external_components:
  - source:
      type: local
      path: /config/esphome/marsrelay/components

mosquitto_broker:
  id: broker
  port: 1883
  max_clients: 10
  on_message:
    - logger.log:
        format: "Broker message: %s => %s"
        args: [topic.c_str(), payload.c_str()]

# Example publish action
interval:
  - interval: 10s
    then:
      - mosquitto_broker.publish_message:
          id: broker
          topic: "relay/hello"
          payload: "ping"
```

## Notes

- The component requires the ESP-IDF framework (ESP32) since it depends on the Espressif Mosquitto
  port from the ESP Component Registry.
- The broker starts listening during `setup()` and processes the loop in `loop()`.

## MarstACK Webserver Component

This repository also provides a `marstack_webserver` component that hosts the same API endpoints as
the upstream MarstACK FastAPI server (excluding static assets and Redoc). It uses ESPHome's built-in
web server base, so you must enable `web_server:` and then add the component:

```yaml
web_server:
  port: 80

marstack_webserver:
  id: marstack_http
  on_request:
    - logger.log:
        format: "HTTP %s %s body=%s from=%s"
        args: [method.c_str(), url.c_str(), body.c_str(), source_ip.c_str()]
```

## Captive DNS Component

The `captive_dns` component provides the DNS redirection logic from ESPHome's captive portal
without hosting the captive portal web server. It listens on UDP port 53 and responds with the
device's soft AP IP for A record queries.

```yaml
captive_dns:
  id: captive_dns_server
```
