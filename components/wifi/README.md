# wifi (forked from ESPHome)

This is a fork of ESPHome's built-in `wifi` component. It exists solely to
allow the **fallback AP / captive portal to run simultaneously with the STA
connection**, which upstream does not support — upstream treats AP mode as a
fallback that shuts down as soon as the STA associates.

## Current base

- **Upstream version:** `2026.6.3`
- **Upstream source:** <https://github.com/esphome/esphome/tree/2026.6.3/esphome/components/wifi>

The pinned version is stored in [`UPSTREAM_VERSION`](./UPSTREAM_VERSION), which
is the single source of truth used by the tooling below.

Everything in this directory is byte-for-byte identical to that upstream
revision except for the single patch in
[`patches/marsrelay-ap-sta.patch`](./patches/marsrelay-ap-sta.patch), which is
also reflected inline in `wifi_component.cpp` (the patch is what actually
ships). CI enforces this with `scripts/check-wifi-fork.sh`.

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

To see (and verify) the exact diff against upstream:

```sh
scripts/check-wifi-fork.sh
```

It downloads pristine upstream at `UPSTREAM_VERSION`, applies the recorded
patch, and asserts every file matches. Expect only the two `// marsrelay:`
hunks in `wifi_component.cpp`.

## Updating to a newer ESPHome release

Updating is git-driven so the patch is carried by a real 3-way merge instead of
being re-applied by hand. From the repo root:

```sh
scripts/update-wifi.sh 2026.X.Y
```

The script:

1. Downloads the pristine upstream `wifi` component at `2026.X.Y` and commits it
   as a **vendor import** (`vendor: import pristine esphome wifi 2026.X.Y`). That
   commit becomes the merge base the *next* update merges against, so it has to
   land in history.
2. Re-applies [`patches/marsrelay-ap-sta.patch`](./patches/marsrelay-ap-sta.patch)
   with `git apply --3way`. Because the patch records its base blob (the previous
   vendor import), git does a true three-way merge: where upstream rewrote a
   patched region you get **standard conflict markers** to resolve, not a silent
   mis-apply.
3. Regenerates the patch against the new base, bumps `UPSTREAM_VERSION`, the CI
   `esphome_version` pin, and the **Current base** above, then runs
   `scripts/check-wifi-fork.sh` to prove the result is exactly upstream + patch.

If the merge is clean the script leaves the re-apply staged for you to review and
commit; if it conflicts it prints the steps to finish manually. Either way, then
validate the build:

```sh
esphome config marsrelay_esp32s3.yaml
esphome config tests/fixtures/shelly_emulator.yaml
pytest -q tests/
git add -A && git commit -m "Re-apply marsrelay patch on esphome wifi 2026.X.Y"
```

> Why this works when upstream changes a lot: the three-way merge has the old
> pristine as its base, so an upstream rewrite of the patched lines surfaces as a
> conflict rather than applying in the wrong place. The CI check
> (`scripts/check-wifi-fork.sh`, workflow `wifi-fork-check`) is the backstop that
> fails the build if the vendored tree ever drifts from "upstream + patch".

## Staying up to date automatically

The `wifi-update-check` workflow (`.github/workflows/wifi-update-check.yml`)
runs daily and via manual dispatch. It uses
[`scripts/check-wifi-upstream.sh`](../../scripts/check-wifi-upstream.sh) to
compare the upstream `wifi` component at the latest ESPHome release against our
pin in [`UPSTREAM_VERSION`](./UPSTREAM_VERSION), and reacts only when the
component actually changed:

- **Unchanged** (no new release, or a release that doesn't touch `wifi`) — the
  job is a green no-op.
- **Changed and the patch still applies** — it runs `scripts/update-wifi.sh` for
  the new version and opens an `automated/update-wifi-<version>` PR for review.
  If such a PR is already open it does nothing (no duplicates).
- **Changed but the patch no longer applies** — it **fails**, signalling that a
  manual `scripts/update-wifi.sh <version>` + conflict resolution is needed.

So the only red state is "upstream changed the component in a way we can't carry
over automatically". Set the optional `WIFI_UPDATE_TOKEN` secret (a PAT) if you
want the generated PR's own CI to run — pushes made with the default
`GITHUB_TOKEN` don't trigger other workflows.

## Why a full fork (instead of a small override)

ESPHome's wifi component doesn't expose an extension point for "keep AP up
after STA connects", and the AP/STA mode transitions are tangled into private
methods of `WiFiComponent`. Subclassing or monkey-patching from a small
component would be more fragile than carrying the two-line patch on a full
fork.
