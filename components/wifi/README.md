# wifi (forked from ESPHome)

This is a fork of ESPHome's built-in `wifi` component. It exists solely to
allow the **fallback AP / captive portal to run simultaneously with the STA
connection**, which upstream does not support — upstream treats AP mode as a
fallback that shuts down as soon as the STA associates.

## Current base

- **Upstream version:** `2026.5.0`
- **Upstream source:** <https://github.com/esphome/esphome/tree/2026.5.0/esphome/components/wifi>

Everything in this directory is byte-for-byte identical to that upstream
revision except for the patches described below.

## The patch (AP + STA simultaneously)

There are exactly **two changes**, both in `wifi_component.cpp`, both marked
with a `// marsrelay:` comment so they're easy to find:

1. **Always start the fallback AP.**
   Upstream only starts the AP after `ap_timeout_` has elapsed since
   `last_connected_`. We start it unconditionally so it's available from boot,
   regardless of STA state.

2. **Keep the AP enabled after STA connects.**
   Upstream calls `this->wifi_mode_({}, false)` to disable the AP once the STA
   associates. We skip that call so AP+STA stay up together.

To see the exact diff against upstream:

```sh
git clone --depth 1 --branch <version> https://github.com/esphome/esphome.git /tmp/esphome
diff -ruN /tmp/esphome/esphome/components/wifi components/wifi
```

Expect only the two `// marsrelay:` hunks in `wifi_component.cpp`.

## Updating to a newer ESPHome release

When the nightly build breaks (or you want to bump ESPHome), re-fork rather
than patching incrementally — upstream changes too much between releases for
hand-merging to be safe:

1. Pick the new upstream version, e.g. `2026.X.Y`.
2. Clone upstream at that tag and copy `esphome/components/wifi/*` over this
   directory (preserve this `README.md`):
   ```sh
   git clone --depth 1 --branch 2026.X.Y https://github.com/esphome/esphome.git /tmp/esphome
   cp /tmp/esphome/esphome/components/wifi/{*.py,*.h,*.cpp} components/wifi/
   ```
3. Re-apply the two `// marsrelay:` patches in `wifi_component.cpp`. Search
   upstream for the anchor lines `ESP_LOGI(TAG, "Starting fallback AP")` and
   `ESP_LOGD(TAG, "Disabling AP")` to locate the spots.
4. Bump `esphome_version` in `.github/workflows/ci.yml` (and ensure the
   nightly's `latest` still resolves to the same major version, or pin it).
5. Verify:
   ```sh
   esphome config marsrelay_esp32s3.yaml
   esphome config tests/fixtures/shelly_emulator.yaml
   pytest -q tests/
   ```
6. Update the **Current base** version at the top of this file.

## Why a full fork (instead of a small override)

ESPHome's wifi component doesn't expose an extension point for "keep AP up
after STA connects", and the AP/STA mode transitions are tangled into private
methods of `WiFiComponent`. Subclassing or monkey-patching from a small
component would be more fragile than carrying the two-line patch on a full
fork.
