#!/usr/bin/env bash
#
# Verify that the vendored ESPHome `wifi` component is exactly:
#
#     pristine upstream @ components/wifi/UPSTREAM_VERSION   +   our patch
#
# i.e. every file matches upstream byte-for-byte, and the only local change is
# components/wifi/patches/marsrelay-ap-sta.patch applied to wifi_component.cpp.
#
# This is the guard that catches a patch landing in the wrong place or accidental
# drift in the vendored files. Run locally or in CI.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
WIFI_DIR="$ROOT/components/wifi"
VERSION="$(cat "${WIFI_DIR}/UPSTREAM_VERSION")"
PATCH="${WIFI_DIR}/patches/marsrelay-ap-sta.patch"
BASE="https://raw.githubusercontent.com/esphome/esphome/${VERSION}/esphome/components/wifi"

# Upstream files (same list as update-wifi.sh) and which of them we patch.
FILES=(
  __init__.py
  automation.h
  wifi_component.cpp
  wifi_component.h
  wifi_component_esp8266.cpp
  wifi_component_esp_idf.cpp
  wifi_component_libretiny.cpp
  wifi_component_pico_w.cpp
  wpa2_eap.py
)

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/components/wifi"

echo "Verifying components/wifi against pristine esphome ${VERSION} + patch"
for f in "${FILES[@]}"; do
  curl -fsSL "${BASE}/${f}" -o "$tmp/components/wifi/${f}"
done

# Apply our patch onto the pristine copy. The patch only touches wifi_component.cpp.
( cd "$tmp" && git apply "$PATCH" )

fail=0
for f in "${FILES[@]}"; do
  if ! diff -u "$tmp/components/wifi/${f}" "${WIFI_DIR}/${f}"; then
    echo "DRIFT: components/wifi/${f} does not match pristine ${VERSION} + patch" >&2
    fail=1
  fi
done

if [[ $fail -ne 0 ]]; then
  echo >&2
  echo "FAILED: vendored wifi component drifted from upstream + patch." >&2
  echo "Re-run scripts/update-wifi.sh ${VERSION} or fix the patch/files." >&2
  exit 1
fi

echo "OK: components/wifi == pristine esphome ${VERSION} + marsrelay patch."
