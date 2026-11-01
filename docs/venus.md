# Venus

What Marsrelay does for Marstek Venus batteries (E, D, A, E v3, and the older Venus E gen 1/2) beyond relaying their MQTT. The full list of cloud requests it answers is in [Cloud emulation](cloud-emulation.md).

## The network dropouts every 15 minutes

Marstek Venus batteries (E, D, E v3) on **control firmware v150** buffer their telemetry and upload it to the Marstek cloud. When an upload goes unacknowledged the buffer never drains — and the firmware then hardware-resets its own network chip on a fixed timer: every 900 s over WiFi, every 1800 s over Ethernet. For two to five seconds the chip is simply gone, so Modbus TCP sessions die, MQTT drops, and even ping stops answering. Then everything comes back, and the clock starts again.

A battery on Marsrelay's access point is a battery on WiFi that cannot reach the cloud, so it is the 900-second variant that applies.

Marsrelay answers that upload. `marstack`'s `https:` block serves `POST /data-upload/v1/venus/<id>` on port 443 with the acknowledgement the firmware waits for, the buffer stays empty, and the reset never fires. It is enabled in [`marsrelay_esp32s3.yaml`](../marsrelay_esp32s3.yaml) and needs no DNS setup of your own — `capture_dns` already points every cloud hostname, `marstekcloud.com` included, at the ESP32:

```yaml
marstack:
  id: marstack_http
  https:
    port: 443
```

Nothing on the battery changes, and no telemetry leaves your network.

What that reply looks like is not a matter of taste. The firmware checks the body with `strstr` for `"code":` followed by `atoi`, so it has to evaluate to **0** — and the framing around it matters just as much: a reply with the same body but ordinary headers in an ordinary order is rejected, after which the battery retries four times and gives up. So `marstack` reproduces the real cloud's response byte for byte (header set, header order, chunked framing) and then holds the connection open for 25 seconds before closing it cleanly, because cutting it earlier makes the firmware throw away a reply it had already received. `raw_responses: false` goes back to ordinary framing if you ever need to compare. The [`https:` settings](cloud-emulation.md#settings) are all optional; `hold_time` in particular is not a knob to turn down.

### Your telemetry, locally

Every upload carries about seventy fields. `on_venus_upload` decodes the ones whose meaning is confirmed and passes the rest through untouched; the example config publishes the result to `<mqtt_topic_prefix>/venus/<deviceId>/telemetry`:

```json
{"soc": 57, "battery_power": -412, "battery_voltage": 52.31, "grid_power": -398,
 "temperature_internal": 24.1, "control_firmware": "150", "unmapped": {"...": "..."}}
```

That is data the battery pushes on its own, separate from the MQTT side — you still want [hm2mqtt](https://github.com/tomquist/hm2mqtt) for Home Assistant entities.

### What this does not fix

The periodic resets stop. A second, smaller interruption does not, and it is worth knowing about before you go looking for it. Since v150 the upload runs over TLS, and a key exchange costs the battery's MCU around four seconds during which it stops serving Modbus — roughly twelve such gaps an hour, one per upload. It keeps answering ping throughout, so it is easy to tell apart from a reset, and it happens with the real cloud too. If you poll the battery over Modbus, raise your client's response timeout above ~8 seconds.

One caveat is specific to serving this from an ESP32: the TLS version range the battery accepts could not be read out of its firmware, and ESP-IDF's mbedTLS no longer implements TLS 1.0 or 1.1, so Marsrelay offers TLS 1.2. If the log shows `TLS handshake with ... failed`, that is the likely reason — please open an issue with the log line.

## Telemetry on older firmware

Before control firmware v150 (and on every Venus E gen 1/2 version) the battery uploads the same telemetry over plain HTTP instead, to `setVenusDReporting` or, on the gen 1/2, `setAcCoupleReporting`. Marsrelay acknowledges those too, the way the real cloud does. Without that the battery keeps every record and sends it again about once a minute. These versions do not reset their network chip over it.

## AI mode

In AI (dynamic tariff) mode a Venus fetches its charge schedule from the cloud every hour. Marsrelay has no schedule to give, so it answers what the real cloud sends a battery without one: an empty schedule. The battery then asks again an hour later instead of every few seconds. AI mode itself does not do anything useful offline.

## Power class (Venus E gen 1/2)

When the Marstek app sends a Venus E gen 1/2 its "active upgrade" command, the battery asks the cloud (`getDevicesApply`) whether it is an 800 W or a 2500 W unit. It stores the answer for good and resets its maximum discharge power to it; as an 800 W unit it also refuses discharge settings above 800 W. The real cloud tells an unknown device 800.

Marsrelay does not guess this, so by default the request goes unanswered and the battery keeps its current class (it repeats the request until its next restart). To answer it, set the class on `marstack:`:

```yaml
marstack:
  power_version: 2500   # 800 or 2500
```

The one value goes to every Venus E gen 1/2 that asks, so set it only if all of them behind this Marsrelay are the same class, and leave it unset if they differ.

Other Venus models send the same request but ignore the answer.
