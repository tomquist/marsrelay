# Device IDs: MAC Address vs. Encrypted ID

Every Marstek battery has a Bluetooth MAC address (12 hex characters, e.g. `009b08a571ee`), shown in the Marstek app and used by [hm2mqtt](https://github.com/tomquist/hm2mqtt) as the `deviceId`. In its cloud MQTT topics, however, a battery on current firmware identifies itself with a long **encrypted ID** instead — only old firmware still uses the plain MAC there, and a firmware update silently switches a battery over (see the [Hame Relay device matrix](https://github.com/tomquist/hame-relay/blob/main/docs/device-matrix.md) for which versions).

Marsrelay forwards topics as-is, so the ID from [step 3 of the setup](../README.md#3-find-your-device-information) is whatever the battery uses. In the rare case that it's the plain MAC: configure it in hm2mqtt and you're done. Otherwise, follow the section for your device family:

## B2500 / Saturn (device types starting with HMA, HMB, HMF, HMK, HMJ)

- **hm2mqtt:** `deviceId` = the **Bluetooth MAC**, never the ID from the topic. hm2mqtt derives its own topic IDs from this value; pasting the topic ID instead sends commands to dead, double-encrypted topics (recognizable as ~96-character IDs in the Marsrelay log).
- **Marsrelay:** map the battery's topic ID to the MAC:

  ```yaml
  mosquitto_broker:
    # ...existing options...
    id_mappings:
      - device: "<id-from-the-device-topic>"   # from step 3
        external: "<bluetooth-mac>"
  ```

Marsrelay rewrites the IDs in both directions and automatically uses the form hm2mqtt expects per topic (plain MAC on `hame_energy/...`, hm2mqtt's encrypted variant on `marstek_energy/...`). The mapping is a harmless no-op when the IDs already match, so just always add it. Requires a current build from `main`; older configs that set the encrypted variant as `external` manually keep working.

> Exception: a B2500 configured for **local MQTT** (e.g. via [hmjs](https://tomquist.github.io/hmjs/)) publishes under its plain MAC — MAC in hm2mqtt, no mapping needed.

## Venus, Jupiter and all other device types (VNS…, JPLS…, HMG…, HMM…, …)

hm2mqtt uses the configured `deviceId` as-is for these devices, so pick one of two options:

- **Option A — encrypted ID in hm2mqtt (simplest):** configure the ID from step 3 as the `deviceId`, e.g. `DEVICE_0=JPLS-8H:<encrypted-id-from-the-device-topic>`.
- **Option B — MAC plus mapping:** keep the Bluetooth MAC as `deviceId` in hm2mqtt and add the same `id_mappings` entry as above (`device:` the encrypted ID, `external:` the MAC).

With multiple batteries, make sure each mapping pairs the IDs of the **same physical battery** — see [Finding the encrypted ID](#finding-the-encrypted-id).

## Finding the encrypted ID

1. **MQTT Explorer:** wait for the battery's `.../device/<deviceId>/ctrl` topic (step 3, 30 minutes or more). With multiple batteries, power all but one off to attribute the IDs unambiguously.
2. **Hame Relay startup logs:** temporarily install [Hame Relay](https://github.com/tomquist/hame-relay) with your Marstek account credentials — on startup it prints each device's MAC and encrypted ID:

   ```text
   Device 1:
     Device ID: 009b08a571ee
     Remote ID: defa85f58f79ab2d2b2818f0a8cd3ee3   <-- the encrypted ID
     Type: HMJ-2
   ```

   This works even if the battery never publishes (or has never connected to Marsrelay); uninstall Hame Relay again afterwards.
