# Agent guide

Marsrelay is an ESPHome project (custom components under `components/`, device
config `marsrelay_esp32s3.yaml`). `CLAUDE.md` is a symlink to this file.

## Dev setup

Work in a virtualenv and install ESPHome pinned to the same version the wifi
fork is based on (`components/wifi/UPSTREAM_VERSION`):

```sh
python3 -m venv .venv && . .venv/bin/activate
pip install -U pip setuptools wheel
pip install "esphome==$(cat components/wifi/UPSTREAM_VERSION)" pytest
```

The venv keeps this isolated from the system Python; on some distros installing
into the system interpreter fails outright (e.g. a broken `setuptools`
`install_layout`), which the venv also sidesteps.

## Validate changes

```sh
esphome config marsrelay_esp32s3.yaml              # main config must be valid
esphome config tests/fixtures/shelly_emulator.yaml # example config must be valid
pytest -q                                          # component tests (run from repo root)
```

`esphome config` validates + generates C++ but does not compile firmware; a full
firmware build (and real-device flash) only happens via `esphome/build-action`
in CI (`.github/workflows/`). Match CI behavior; don't loosen the version pin.

## The vendored `wifi` fork

`components/wifi/` is upstream ESPHome's `wifi` component plus one patch
(`patches/marsrelay-ap-sta.patch`) that keeps the fallback AP up alongside STA.
Do not hand-edit the vendored files to update ESPHome. Instead:

```sh
scripts/update-wifi.sh <esphome_version>   # bump + 3-way re-apply the patch
scripts/check-wifi-fork.sh                 # assert tree == upstream + patch (also runs in CI)
```

See `components/wifi/README.md` for the full rationale.
