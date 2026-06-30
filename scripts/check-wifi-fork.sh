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
#
# The component's file set is discovered from the upstream folder (see
# scripts/lib-wifi.sh), so an upstream file added or removed is caught here
# instead of slipping past a hardcoded list.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
. "$(dirname "${BASH_SOURCE[0]}")/lib-wifi.sh"

WIFI_DIR="$ROOT/components/wifi"
VERSION="$(cat "${WIFI_DIR}/UPSTREAM_VERSION")"
PATCH="${WIFI_DIR}/patches/marsrelay-ap-sta.patch"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/components/wifi"

echo "Verifying components/wifi against pristine esphome ${VERSION} + patch"
wifi_download_upstream "$VERSION" "$tmp/components/wifi"

# Apply our patch onto the pristine copy. The patch only touches wifi_component.cpp.
if ! ( cd "$tmp" && git apply "$PATCH" ); then
  echo >&2
  echo "FAILED: $(basename "$PATCH") does not apply to pristine esphome ${VERSION}." >&2
  echo "The patch is stale or incompatible with upstream; re-run scripts/update-wifi.sh ${VERSION}." >&2
  exit 1
fi

fail=0

# 1. The vendored file set must match the upstream folder (catches a vendored
#    file that upstream added or removed).
upstream_set="$(wifi_upstream_files "$VERSION")"
vendored_set="$(wifi_vendored_files "$ROOT")"
if [[ "$upstream_set" != "$vendored_set" ]]; then
  echo "DRIFT: vendored file set differs from upstream ${VERSION}:" >&2
  diff <(printf '%s\n' "$upstream_set") <(printf '%s\n' "$vendored_set") \
    | sed -n 's/^< /  missing from vendored: /p; s/^> /  not in upstream: /p' >&2
  fail=1
fi

# 2. Each upstream file must match the vendored copy byte-for-byte (post-patch).
while IFS= read -r f; do
  [[ -n "$f" ]] || continue
  if [[ ! -f "${WIFI_DIR}/${f}" ]]; then
    echo "DRIFT: components/wifi/${f} is missing from the vendored tree" >&2
    fail=1
  elif ! diff -u "$tmp/components/wifi/${f}" "${WIFI_DIR}/${f}"; then
    echo "DRIFT: components/wifi/${f} does not match pristine ${VERSION} + patch" >&2
    fail=1
  fi
done <<< "$upstream_set"

if [[ $fail -ne 0 ]]; then
  echo >&2
  echo "FAILED: vendored wifi component drifted from upstream + patch." >&2
  echo "Re-run scripts/update-wifi.sh ${VERSION} or fix the patch/files." >&2
  exit 1
fi

echo "OK: components/wifi == pristine esphome ${VERSION} + marsrelay patch."
