# Agent guide

Marsrelay is an ESPHome project (custom components under `components/`, device
config `marsrelay_esp32s3.yaml`). `CLAUDE.md` is a symlink to this file.

## Dev setup

Work in a virtualenv and install ESPHome pinned to the version CI uses
(`ESPHOME_VERSION`):

```sh
python3 -m venv .venv && . .venv/bin/activate
pip install -U pip setuptools wheel
pip install "esphome==$(cat ESPHOME_VERSION)" pytest
```

The venv keeps this isolated from the system Python; on some distros installing
into the system interpreter fails outright (e.g. a broken `setuptools`
`install_layout`), which the venv also sidesteps.

## Validate changes

```sh
scripts/local-config.sh                            # main config, pointed at this checkout
esphome config marsrelay_esp32s3.local.yaml        # main config must be valid
esphome config tests/fixtures/shelly_emulator.yaml # example config must be valid
pytest -q                                          # component tests (run from repo root)
```

`marsrelay_esp32s3.yaml` is what users download, so it fetches the components
from `github://tomquist/marsrelay@main`. Validating it directly tests `main`,
not your changes; `scripts/local-config.sh` writes a gitignored copy that uses
`./components` instead. Keep the shipped source line as is: the script, the
tests and CI's `build-remote` job all rewrite it.

`esphome config` validates + generates C++ but does not compile firmware; a full
firmware build (and real-device flash) only happens via `esphome/build-action`
in CI (`.github/workflows/`). Match CI behavior; don't loosen the version pin.

## Keeping the access point up

ESPHome's `wifi` AP is a fallback by default: it shuts down once the station
connects, but the batteries and meters on the Marsrelay's AP need it to stay.
Every Marsrelay component auto-loads `components/marsrelay_ap`, which switches
`wifi` to `ap_mode: ALWAYS_ON` and `post_connect_roaming: false` (ESP32 only)
during final validation. Users' downloaded YAML pulls every component from this
repo, so they get it without editing their config. A new component that serves
devices on the AP should auto-load it too.

`components/wifi/__init__.py` is not a fork: it only loads ESPHome's own `wifi`
component in its place. Marsrelay used to ship a fork there, and ESPHome's
cached copy of this repo keeps that fork's `__pycache__` after an update, which
ESPHome would otherwise load as a broken `wifi` component. Don't delete it.
