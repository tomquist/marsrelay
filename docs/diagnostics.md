# Diagnostics

Marsrelay can publish its own state as Home Assistant entities: whether the
broker and the UDP proxy are up, how many messages and packets each has moved,
and how long ago the battery last sent something. They are useful when the
device is still reachable but data has stopped arriving.

All of them are `entity_category: diagnostic`, so Home Assistant files them
under the device's diagnostics, and all of them are optional.
[`marsrelay_esp32s3.yaml`](../marsrelay_esp32s3.yaml) enables a subset; add or
remove whatever you need.

```yaml
binary_sensor:
  - platform: mosquitto_broker
    mosquitto_broker_id: local_broker
    device_active:
      name: "Battery MQTT data"
      timeout: 15min      # how long the battery may stay silent

sensor:
  - platform: udp_proxy
    udp_proxy_id: meter_proxy
    packets_to_sta:
      name: "Meter requests forwarded"
```

| Entity | Platform | Reports |
|---|---|---|
| `running` | `mosquitto_broker` | the embedded broker task is running |
| `device_active` | `mosquitto_broker` | the battery published on a `.../device/...` topic within `timeout` (default `15min`) |
| `publish_client_connected` | `mosquitto_broker` | the internal client that relays commands to the battery is connected |
| `device_messages` / `app_messages` | `mosquitto_broker` | messages received from the battery / relayed towards it |
| `publish_errors` / `broker_restarts` | `mosquitto_broker` | publishes that failed, and how often the broker was restarted |
| `device_message_age` | `mosquitto_broker` | seconds since the last battery message |
| `active` | `udp_proxy` | both sockets are bound |
| `meter_responding` | `udp_proxy` | the power meter answered within `timeout` (default `5min`) |
| `packets_to_sta` / `packets_to_ap` | `udp_proxy` | packets forwarded to the home network / to the access point |
| `packets_dropped` / `sessions` | `udp_proxy` | packets not forwarded, and clients with an active session |
| `request_age` / `response_age` | `udp_proxy` | seconds since the last packet in each direction |
| `device_active` | `marstack` | the battery called a cloud endpoint within `timeout` (default `30min`) |
| `requests` / `venus_uploads` | `marstack` | cloud requests and telemetry uploads answered |
| `request_age` | `marstack` | seconds since the last cloud request |

Each component refreshes its own entities every `diagnostics_interval`
(default `60s`, settable on `mosquitto_broker:`, `udp_proxy:` and `marstack:`).
Counters reset on reboot and are reported as `total_increasing`. The `*_age`
sensors stay *unknown* until the first message of that kind arrives.

A few values are easier to read in combination:

- The battery calls the `marstack` HTTP endpoints on a schedule of its own,
  separate from MQTT, so `marstack`'s `device_active` and the broker's can
  differ. HTTP active with MQTT inactive means the battery still reaches
  Marsrelay but its MQTT session is gone; both inactive means it is not
  reaching Marsrelay at all.
- `running` off means the broker task exited. It is restarted automatically,
  and `broker_restarts` counts how often that happened.
- `publish_errors` counts commands that did not reach the battery.
- `meter_responding` off while `active` is on means the proxy is working and
  nothing on the home network answered.

The `status` binary sensor in the example config is ESPHome's own: over MQTT it
reflects the connection to your home broker through the last will.

## Acting on silence

The same signals are available as automations, so Marsrelay can react on its
own. The trigger always runs on the device, but what an action achieves depends
on what it needs: the `mqtt.publish` below goes to your home broker, so it only
arrives while that broker is reachable. The two actions further down need
nothing outside the device, which is what makes them useful when Home Assistant
is the part that went away.

```yaml
mosquitto_broker:
  id: local_broker
  on_device_timeout:
    timeout: 15min
    then:
      - mqtt.publish:
          topic: marsrelay/status/device_stale
          payload: "ON"
  on_device_recovered:
    timeout: 15min
    then:
      - mqtt.publish:
          topic: marsrelay/status/device_stale
          payload: "OFF"
```

| Trigger | Component | Fires when |
|---|---|---|
| `on_device_timeout` / `on_device_recovered` | `mosquitto_broker` | the battery stops publishing on a `.../device/...` topic for `timeout` (default `15min`), and when it starts again |
| `on_meter_timeout` / `on_meter_recovered` | `udp_proxy` | the power meter stops answering for `timeout` (default `5min`), and when it answers again |
| `on_device_timeout` / `on_device_recovered` | `marstack` | the battery stops calling the cloud endpoints for `timeout` (default `30min`), and when it starts again |

Each automation carries its own `timeout:` and its own state, so one config can
note a short silence and act on a longer one. They are independent of the
binary sensors' timeouts and of `diagnostics_interval`, including `never`.

A timeout does not fire before the signal has been active once, so a reboot
does not look like a loss, and a recovery fires only after a timeout did.

Two actions that need nothing outside the device:

```yaml
# Send the battery a command through the local broker. The payload is
# device-specific -- see hm2mqtt for what a given model accepts.
mosquitto_broker:
  id: local_broker
  on_device_timeout:
    timeout: 90min
    then:
      - mosquitto_broker.publish_message:
          id: local_broker
          topic: "marstek_energy/<deviceType>/App/<deviceId>/ctrl"
          payload: "cd=07"

# Or restart the relay, using ESPHome's own restart switch. The condition keeps
# it to the case where both sides went quiet, since either one can stop on its
# own.
switch:
  - platform: restart
    id: restart_switch
    name: "Restart"

binary_sensor:
  - platform: mosquitto_broker
    mosquitto_broker_id: local_broker
    device_active:
      id: battery_mqtt_data
      name: "Battery MQTT data"

marstack:
  id: marstack_http
  on_device_timeout:
    timeout: 45min
    then:
      - if:
          condition:
            binary_sensor.is_off: battery_mqtt_data
          then:
            - switch.turn_on: restart_switch
```

A restart cannot end up in a loop: after the reboot the battery has to be seen
again before another timeout can fire, so a battery that is simply switched off
triggers one restart and no more.

Neither timeout on its own proves the battery is gone. The HTTP side can go
quiet while MQTT keeps working, and the other way round — that is the same
split the [diagnostics](#diagnostics) notes describe — so a restart is worth
gating on both, as above.
