# Troubleshooting

## "Platform not found" or "Component not found" when validating the config

ESPHome can't find a Marsrelay component the YAML uses, e.g. `Platform not found: 'binary_sensor.mosquitto_broker'`. The config is newer than the components ESPHome is building with. Check the `external_components:` source in your YAML:

- **`type: local` / `path: ./components`:** your copy of the `components` folder is older than the config. Switch to the GitHub source instead (see [Flash Marsrelay](../README.md#1-flash-marsrelay)):

  ```yaml
  external_components:
    - source: github://tomquist/marsrelay@main
  ```

- **`github://tomquist/marsrelay@main`:** ESPHome re-downloads the components only once a day, so right after an update it may still use its cached copy. Add `refresh: always` next to `source:`, validate once, then remove it again:

  ```yaml
  external_components:
    - source: github://tomquist/marsrelay@main
      refresh: always
  ```

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

**Cause 2: The device ID in the topic doesn't match what hm2mqtt publishes to.** Newer firmware switches the battery from its plain MAC to an [encrypted ID](device-ids.md) (e.g. Jupiter C+ with firmware v142), so this often shows up right after a firmware update:

- **Venus/Jupiter and other non-B2500 devices:** find the battery's encrypted ID (see [finding the encrypted ID](device-ids.md#finding-the-encrypted-id)) and either configure it as the `deviceId` in hm2mqtt or add an `id_mappings` entry in Marsrelay.
- **B2500/Saturn devices (HMA/HMB/HMF/HMK/HMJ):** keep the **Bluetooth MAC** as `deviceId` in hm2mqtt and add an `id_mappings` entry mapping the battery's topic ID to that MAC (requires a current Marsrelay build). If you pasted the battery's topic ID into hm2mqtt instead, that's the problem: commands end up on a double-encrypted topic (~96-character IDs in the Marsrelay log).

## No /device/ topic ever appears

Work through this checklist:

1. **Are you looking at the right topics?** Only `.../device/...` topics come from the battery — `.../App/...` topics are commands *from* hm2mqtt. Watch both the `marstek_energy` and `hame_energy` prefixes.
2. **Power-cycle the battery once Marsrelay is up.** The restart wipes the battery's DNS cache: it looks the cloud hostnames up again and gets Marsrelay's IP instead of the cached one. A battery that joined the access point before Marsrelay was running can otherwise sit in a stale state for a long time. Boot the ESP32 first, give it a minute, then switch the battery off and on again — do this before you start waiting.
3. **Wait long enough after that power cycle.** The battery usually publishes within 20 minutes, but half an hour or more happens ([#42](https://github.com/tomquist/marsrelay/issues/42)). Keep MQTT Explorer connected the whole time.
4. **Is the battery really connected to the Marsrelay access point?** The Marstek app sometimes reports a successful WiFi change even though the battery didn't connect. When it is connected, the Marsrelay log shows its HTTP requests, e.g. `HTTP GET /app/neng/getDateInfoeu.php`. If those appear but no `/device/` topic does, the battery is talking to Marsrelay and only MQTT is still pending — keep waiting and check the next points.
5. **Can Marsrelay reach your home MQTT broker?** Verify `mqtt_broker`, `mqtt_username` and `mqtt_password` in the substitutions, and check the ESPHome logs for MQTT connection errors.
6. **Is the ESP32 stable?** Watch the logs via USB for a crash loop, and make sure the `psram` settings match your board.

If you only need the device ID while you keep debugging, check the Marsrelay log for `Battery connected on 'marstek_energy/<deviceType>/App/<deviceId>/ctrl'` (on a Venus, logged as soon as the battery's MQTT connection is up), or get it from Hame Relay's startup logs instead of waiting (see [finding the encrypted ID](device-ids.md#finding-the-encrypted-id)).

## Repeating getDateInfoeu.php requests and UDP proxy log lines

Not a problem — that's healthy operation. `HTTP GET /app/neng/getDateInfoeu.php` is the battery checking in with the (emulated) cloud, and the `udp_proxy` lines are power meter packets being bridged. A few related things that are easy to misread:

- Messages on `<mqtt_topic_prefix>/request` (e.g. `marsrelay/request`) are **diagnostic logs** of the battery's HTTP requests — not battery data.
- A B2500 calling `getRealtimeSoc` about once a minute is also expected: it does that after its controller restarted itself, until its next restart (see [B2500](b2500.md#charge-level-after-a-controller-restart)).
- `No answer for GET /...; replying 404` means the battery called a cloud endpoint Marsrelay does not emulate yet. Please open an issue with that line and the `query params` line logged with it, replacing the values (such as `devid`, which can be your battery's MAC address) with placeholders.
- The battery publishes under `.../device/...` on its own only now and then. Steady data needs [hm2mqtt](https://github.com/tomquist/hm2mqtt), which polls it (`cd=1`). Until the battery's first message, seeing nothing but `request` messages is expected.

## A Venus keeps dropping off the network

Symptoms: the battery vanishes for two to five seconds on a fixed rhythm — Modbus TCP times out, Home Assistant shows it unavailable, even ping stops answering — and then it is back until the next time.

That is the battery's own firmware, not your network. On Venus control firmware **v150** the battery buffers telemetry records and hardware-resets its network chip whenever the buffer is not being drained by uploads to the Marstek cloud: every 900 s on WiFi, which is how a battery reaches Marsrelay. Marsrelay answers those uploads so the buffer stays empty; see [the network dropouts every 15 minutes](venus.md#the-network-dropouts-every-15-minutes).

If it still happens:

1. **Is the TLS listener configured?** The `marstack:` block needs an `https:` section — configs created before it existed don't have one. Check for `Serving the Marstek cloud over TLS on port 443` in the log at boot.
2. **Are uploads arriving and being answered?** Look for `POST /data-upload/v1/venus/<id>` in the log, or watch `<mqtt_topic_prefix>/venus/<deviceId>/telemetry`. Once the backlog has drained, uploads settle to roughly one per 300 s. A rigid one-per-60-s rhythm means the buffer is still above three records and the firmware is throttling itself — that clears on its own, but it can take the better part of an hour.
3. **Does the handshake fail?** `TLS handshake with ... failed` means the battery and ESP-IDF's mbedTLS could not agree on a TLS version or cipher suite. ESP-IDF no longer implements TLS 1.0 or 1.1, so Marsrelay offers 1.2. Please open an issue with that log line.
4. **Keep Marsrelay up.** A record is buffered every five minutes and only removed when an upload is answered, so an ESP32 that reboots repeatedly can let the backlog build back over the threshold.

Note that this does not remove *every* interruption. Since v150 each upload runs over TLS, and the key exchange stops the battery serving Modbus for about four seconds — roughly twelve times an hour, one per upload, with the real cloud too. It keeps answering ping throughout, which is how you tell the two apart. Raise your Modbus client's response timeout above ~8 seconds.

## Communication stops after a few hours, or the log shows broker crashes

If the log shows `***ERROR*** A stack overflow in task mosq_broker` or `Unable to accept new connection, system socket count has been exceeded`, first update to the latest `main` — earlier versions of the embedded broker had crash bugs that have been fixed. Beyond that:

- The [diagnostic entities](diagnostics.md) show which part stopped: `Local broker running` and `Broker restarts` for the broker, `Battery MQTT data` and `Battery HTTP requests` for the battery's two connections.
- Some users report better long-term stability with `CONFIG_LWIP_MAX_SOCKETS: "32"` and `CONFIG_LWIP_SOCKET_OFFSET: "16"` in the `sdkconfig_options`.
- Since a build from `main`, a broker that exits on its own is started again automatically (the log says `Broker task exited on its own; restarting it in ... ms`). Before that it stayed down until the next reboot, which is one way the symptom below could be reached. Seeing that line repeatedly means the broker keeps dying — the settings in this list still apply.
- A power cycle of the ESP32 restores communication as a stopgap. The [liveness automations](diagnostics.md#acting-on-silence) can do that, or send the battery a command, without Home Assistant being involved.
- The log line `TLS client: certificate verification relaxed (CN check disabled, ...)` is expected with `tls_skip_verification: true` and is not an error.
- If crashes persist, double-check the `psram` settings match your board, and consider a standard ESP32-S3 devkit board.

Long-running dropouts where MQTT and UDP stop together while WiFi stays up are still being investigated in [issue #10](https://github.com/tomquist/marsrelay/issues/10). The UDP proxy also retries its own start now, so a proxy whose sockets never came up no longer needs a reboot either.

## A B2500 stops answering about 30 minutes after a reset

Symptoms: after a battery reset everything works, then data stops for good 20–35 minutes later. `Commands relayed` keeps counting while `Battery messages` stands still, hm2mqtt reports no response, and restarting Marsrelay does not help. Only resetting the battery does, until the next time.

Seen on the B2500 (HMJ, firmware V116). About once a minute the battery reports to the cloud with a request whose address is about 1 KB long, more than the ESP32's web server accepted by default (512 bytes). The rejected reports pile up, the battery eventually resets its own WiFi, and after that it never reconnects to MQTT. `marstack` now raises the web server's limits to 2048 bytes, so update to the latest `main` and rebuild. If you set `CONFIG_HTTPD_MAX_URI_LEN` or `CONFIG_HTTPD_MAX_REQ_HDR_LEN` yourself, a smaller value is raised to that and a larger one is kept.

If it still happens:

- Look for `Rejected a request that is too long for the web server` in the log. It means a request is longer still; please open an issue with that line.
- The log shows every client joining and leaving the access point, with the reason it left (`AP client <mac> left (reason ...)`), which tells whether the battery drops off WiFi first.

Discussed in [issue #66](https://github.com/tomquist/marsrelay/issues/66).

## The battery can't reach a power meter on my home network (e.g. EcoTracker)

The Marsrelay access point is a separate network, so devices on your home LAN are not directly reachable from the battery. `udp_proxy` bridges exactly one thing: **UDP broadcasts** of the supported meter protocols (Shelly, CT00x). TCP-polled meters — such as the everHome EcoTracker — cannot be bridged this way ([issue #16](https://github.com/tomquist/marsrelay/issues/16)).

Workaround: bring the meter reading into ESPHome as a sensor (e.g. imported from Home Assistant) and serve it to the battery with the [Shelly UDP emulator](shelly-emulator.md) directly on the ESP32.

A Marstek CT that is itself on a Marsrelay access point needs `meter_network: access_point` on that Marsrelay's `udp_proxy`; see [A Marstek CT on its own Marsrelay](marstek-ct.md).

## hm2mqtt shows all entities as unavailable

hm2mqtt is waiting for data under a `deviceId` that doesn't correspond to what the battery publishes. Check the configured `deviceId` against the ID in the battery's `.../device/<deviceId>/...` topic: for B2500/Saturn devices it must be the Bluetooth MAC plus an `id_mappings` entry mapping the battery's topic ID to that MAC; for all other devices it must match the topic ID exactly (or be connected via `id_mappings`). See [Device IDs](device-ids.md).

## Can I run Marsrelay on a Raspberry Pi instead of an ESP32?

Not with this codebase — it's an ESPHome project. But the concept ports: you'd need a WiFi hotspot, a DNS server that resolves every query to the Pi itself, an HTTP server implementing the Marstek cloud endpoints on port 80 plus a TLS one on 443 for the Venus telemetry upload (see `components/marstack/`), and a TLS-enabled MQTT broker on port 8883 that bridges to your main broker (see `components/mosquitto_broker/`). See [How It Works](../README.md#how-it-works) for how the pieces fit together.
