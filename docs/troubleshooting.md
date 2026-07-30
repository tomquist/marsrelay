# Troubleshooting

## The battery doesn't react to commands (e.g. cd=1)

Marsrelay's log shows `Received control message: ...` but the battery never answers: the command reaches Marsrelay but doesn't match the topic the battery listens on. Compare the `.../device/<deviceId>/...` topic the battery publishes on (see [finding your device information](../README.md#3-find-your-device-information)) with the topic your commands arrive on. Two common causes:

**Cause 1: Wrong topic prefix (`hame_energy` vs. `marstek_energy`).** Older firmware uses `hame_energy/...`, newer firmware `marstek_energy/...`. The current example config forwards commands for both, but configs created from an older version only subscribe `marstek_energy/+/App/+/ctrl`. If your battery's `/device/` topics appear under `hame_energy/...`, make sure that prefix is subscribed too in the `mqtt.on_connect` lambda:

```yaml
mqtt:
  # ...
  on_connect:
    - lambda: |-
        id(mqtt_client).subscribe("hame_energy/+/App/+/ctrl", [=](const std::string &topic, const std::string &payload) {
          ESP_LOGD("MQTT", "Received control message: %s", topic.c_str());
          id(local_broker).publish_message(topic, payload);
        });
        // keep the existing marstek_energy subscription from the example config here as well
```

The outgoing direction needs no change: the `mosquitto_broker` `on_message` forwarding matches any topic containing `/device/`, regardless of prefix.

**Cause 2: The device ID in the topic doesn't match what hm2mqtt publishes to.** Newer firmware switches the battery from its plain MAC to an [encrypted ID](../README.md#device-ids-mac-address-vs-encrypted-id) (e.g. Jupiter C+ with firmware v142), so this often shows up right after a firmware update:

- **Venus/Jupiter and other non-B2500 devices:** find the battery's encrypted ID (see [finding the encrypted ID](../README.md#finding-the-encrypted-id)) and either configure it as the `deviceId` in hm2mqtt or add an `id_mappings` entry in Marsrelay.
- **B2500/Saturn devices (HMA/HMB/HMF/HMK/HMJ):** keep the **Bluetooth MAC** as `deviceId` in hm2mqtt and add an `id_mappings` entry mapping the battery's topic ID to that MAC (requires a current Marsrelay build). If you pasted the battery's topic ID into hm2mqtt instead, that's the problem: commands end up on a double-encrypted topic (~96-character IDs in the Marsrelay log).

## No /device/ topic ever appears

Work through this checklist:

1. **Are you looking at the right topics?** Only `.../device/...` topics come from the battery — `.../App/...` topics are commands *from* hm2mqtt. Watch both the `marstek_energy` and `hame_energy` prefixes.
2. **Wait long enough.** The battery usually publishes within 20 minutes, but half an hour or more happens ([#42](https://github.com/tomquist/marsrelay/issues/42)). Keep MQTT Explorer connected the whole time.
3. **Power-cycle the battery once Marsrelay is up.** A battery that joined the access point before Marsrelay was running can sit in a stale state for a long time. Boot the ESP32 first, give it a minute, then switch the battery off and on again — and start the waiting time from there.
4. **Is the battery really connected to the Marsrelay access point?** The Marstek app sometimes reports a successful WiFi change even though the battery didn't connect. When it is connected, the Marsrelay log shows its HTTP requests, e.g. `HTTP GET /app/neng/getDateInfoeu.php`. If those appear but no `/device/` topic does, the battery is talking to Marsrelay and only MQTT is still pending — keep waiting and check the next points.
5. **Can Marsrelay reach your home MQTT broker?** Verify `mqtt_broker`, `mqtt_username` and `mqtt_password` in the substitutions, and check the ESPHome logs for MQTT connection errors.
6. **Is the ESP32 stable?** Watch the logs via USB for a crash loop, and make sure the `psram` settings match your board.

If you only need the device ID while you keep debugging, get it from Hame Relay's startup logs instead of waiting (see [finding the encrypted ID](../README.md#finding-the-encrypted-id)).

## Repeating getDateInfoeu.php requests and UDP proxy log lines

Not a problem — that's healthy operation. `HTTP GET /app/neng/getDateInfoeu.php` is the battery checking in with the (emulated) cloud, and the `udp_proxy` lines are power meter packets being bridged. Two related things that are easy to misread:

- Messages on `<mqtt_topic_prefix>/request` (e.g. `marsrelay/request`) are **diagnostic logs** of the battery's HTTP requests — not battery data.
- The battery does **not** push telemetry on its own. Data only appears under `.../device/...` when something requests it — which is what [hm2mqtt](https://github.com/tomquist/hm2mqtt) does (e.g. `cd=1`). Without hm2mqtt, seeing nothing but `request` messages is expected.

## Communication stops after a few hours, or the log shows broker crashes

If the log shows `***ERROR*** A stack overflow in task mosq_broker` or `Unable to accept new connection, system socket count has been exceeded`, first update to the latest `main` — earlier versions of the embedded broker had crash bugs that have been fixed. Beyond that:

- Some users report better long-term stability with `CONFIG_LWIP_MAX_SOCKETS: "32"` and `CONFIG_LWIP_SOCKET_OFFSET: "16"` in the `sdkconfig_options`.
- A power cycle of the ESP32 restores communication as a stopgap.
- The log line `TLS client: certificate verification relaxed (CN check disabled, ...)` is expected with `tls_skip_verification: true` and is not an error.
- If crashes persist, double-check the `psram` settings match your board, and consider a standard ESP32-S3 devkit board.

Long-running dropouts where MQTT and UDP stop together while WiFi stays up are still being investigated in [issue #10](https://github.com/tomquist/marsrelay/issues/10).

## The battery can't reach a power meter on my home network (e.g. EcoTracker)

The Marsrelay access point is a separate network, so devices on your home LAN are not directly reachable from the battery. `udp_proxy` bridges exactly one thing: **UDP broadcasts** of the supported meter protocols (Shelly, CT00x). TCP-polled meters — such as the everHome EcoTracker — cannot be bridged this way ([issue #16](https://github.com/tomquist/marsrelay/issues/16)).

Workaround: bring the meter reading into ESPHome as a sensor (e.g. imported from Home Assistant) and serve it to the battery with the [Shelly UDP emulator](../README.md#optional-shelly-udp-emulator) directly on the ESP32.

## hm2mqtt shows all entities as unavailable

hm2mqtt is waiting for data under a `deviceId` that doesn't correspond to what the battery publishes. Check the configured `deviceId` against the ID in the battery's `.../device/<deviceId>/...` topic: for B2500/Saturn devices it must be the Bluetooth MAC plus an `id_mappings` entry mapping the battery's topic ID to that MAC; for all other devices it must match the topic ID exactly (or be connected via `id_mappings`). See [Device IDs](../README.md#device-ids-mac-address-vs-encrypted-id).

## Can I run Marsrelay on a Raspberry Pi instead of an ESP32?

Not with this codebase — it's an ESPHome project. But the concept ports: you'd need a WiFi hotspot, a DNS server that resolves every query to the Pi itself, an HTTP server implementing the Marstek cloud endpoints (see `components/marstack/`), and a TLS-enabled MQTT broker on port 8883 that bridges to your main broker (see `components/mosquitto_broker/`). See [How It Works](../README.md#how-it-works) for how the pieces fit together.
