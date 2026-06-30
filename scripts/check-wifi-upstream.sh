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
. "$(dirname "${BASH_SOURCE[0]}")/lib-wifi.sh"

WIFI_DIR="$ROOT/components/wifi"
CURRENT="$(cat "${WIFI_DIR}/UPSTREAM_VERSION")"

emit() {  # publish a key=value pair to GITHUB_OUTPUT when running under Actions
  if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    echo "$1=$2" >> "$GITHUB_OUTPUT"
  fi
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

# Compare the two upstream component folders wholesale, so a file added, removed,
# or changed between the versions all count as a change.
wifi_download_upstream "$CURRENT" "$tmp/cur"
wifi_download_upstream "$LATEST" "$tmp/new"

if diff -rq "$tmp/cur" "$tmp/new" >/dev/null; then
  changed=false
  echo "==> esphome ${LATEST} is newer but the wifi component is unchanged."
else
  changed=true
  echo "==> The wifi component changed between ${CURRENT} and ${LATEST}:"
  # Informational only; diff exits non-zero when files differ, so don't let it
  # trip `set -e` (|| true). Rewrite the temp paths into readable file notes.
  diff -rq "$tmp/cur" "$tmp/new" \
    | sed -E "s#Files ${tmp}/cur/(.*) and ${tmp}/new/.* differ#  changed: \1#; \
              s#Only in ${tmp}/cur: (.*)#  removed in ${LATEST}: \1#; \
              s#Only in ${tmp}/new: (.*)#  added in ${LATEST}: \1#" || true
fi
emit changed "$changed"
