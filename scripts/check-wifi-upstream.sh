#!/usr/bin/env bash
#
# Detect whether a newer ESPHome release has changed the upstream `wifi`
# component relative to the version this fork is pinned to
# (components/wifi/UPSTREAM_VERSION).
#
# "Changed" means upstream actually touched one of the component's files. A
# release that is newer but leaves the wifi component byte-for-byte identical is
# a no-op for this fork and is reported as unchanged -- there is nothing to
# re-vendor, so we don't want to nag about it.
#
# This script only *detects*; it never bumps anything and exits 0 in both the
# changed and unchanged cases. The daily workflow (.github/workflows/
# wifi-update-check.yml) decides what to do with the result.
#
# When run under GitHub Actions it appends results to $GITHUB_OUTPUT:
#   latest=<version>      latest esphome release on PyPI
#   changed=<true|false>  whether the wifi component differs (pinned vs latest)
# Locally it just prints a human-readable summary.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
WIFI_DIR="$ROOT/components/wifi"
CURRENT="$(cat "${WIFI_DIR}/UPSTREAM_VERSION")"

# Same upstream file set as update-wifi.sh / check-wifi-fork.sh -- keep in sync
# with the upstream directory listing.
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

emit() {  # publish a key=value pair to GITHUB_OUTPUT when running under Actions
  if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    echo "$1=$2" >> "$GITHUB_OUTPUT"
  fi
}

raw_url() {  # raw_url <version> <file>
  echo "https://raw.githubusercontent.com/esphome/esphome/$1/esphome/components/wifi/$2"
}

echo "Current pinned esphome wifi version: ${CURRENT}"

LATEST="$(curl -fsSL https://pypi.org/pypi/esphome/json \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["info"]["version"])')"
echo "Latest esphome release on PyPI:      ${LATEST}"
emit latest "$LATEST"

if [[ "$CURRENT" == "$LATEST" ]]; then
  echo "Already pinned to the latest release; nothing to check."
  emit changed false
  exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

changed=false
for f in "${FILES[@]}"; do
  curl -fsSL "$(raw_url "$CURRENT" "$f")" -o "$tmp/cur-$f"
  curl -fsSL "$(raw_url "$LATEST" "$f")" -o "$tmp/new-$f"
  if ! diff -q "$tmp/cur-$f" "$tmp/new-$f" >/dev/null; then
    echo "  changed upstream: $f"
    changed=true
  fi
done

if [[ "$changed" == true ]]; then
  echo "==> The wifi component changed between ${CURRENT} and ${LATEST}."
else
  echo "==> esphome ${LATEST} is newer but the wifi component is unchanged."
fi
emit changed "$changed"
