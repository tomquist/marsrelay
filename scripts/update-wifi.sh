#!/usr/bin/env bash
#
# Update the vendored ESPHome `wifi` component to a new upstream release and
# re-apply the marsrelay AP+STA patch using git's 3-way merge.
#
# Usage:
#   scripts/update-wifi.sh <esphome_version>      # e.g. scripts/update-wifi.sh 2026.7.0
#
# How it works (see components/wifi/README.md for the full rationale):
#   1. Downloads the pristine upstream wifi component at <esphome_version> and
#      commits it as a "vendor import". That commit is the merge base the *next*
#      update will 3-way-merge against, so it must land in history.
#   2. Re-applies components/wifi/patches/marsrelay-ap-sta.patch with
#      `git apply --3way`. Where upstream rewrote a patched region you get
#      standard git conflict markers instead of a silent mis-apply.
#   3. Regenerates the patch against the new base and bumps the version markers
#      (UPSTREAM_VERSION, the CI pin, and the README), then verifies the result.
#
# The vendor import is committed automatically; the re-apply is left staged for
# you to review and commit.
set -euo pipefail

VERSION="${1:-}"
if [[ -z "$VERSION" ]]; then
  echo "usage: $0 <esphome_version>   (e.g. 2026.7.0)" >&2
  exit 2
fi

ROOT="$(git rev-parse --show-toplevel)"
WIFI_DIR="$ROOT/components/wifi"
PATCH="$WIFI_DIR/patches/marsrelay-ap-sta.patch"
BASE="https://raw.githubusercontent.com/esphome/esphome/${VERSION}/esphome/components/wifi"

# The upstream files that make up the component. Keep this in sync with the
# upstream directory listing; everything else under components/wifi/ (README.md,
# UPSTREAM_VERSION, patches/) is ours and is never overwritten.
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

# Require a clean components/wifi tree so the vendor-import commit below captures
# only the freshly downloaded upstream files (git add stages all of WIFI_DIR).
# Use status --porcelain (not diff-index) so stat-only changes don't false-abort.
if [[ -n "$(git -C "$ROOT" status --porcelain -- "$WIFI_DIR")" ]]; then
  echo "ERROR: $WIFI_DIR has uncommitted changes; commit or stash them first." >&2
  exit 1
fi

echo "==> Downloading pristine esphome wifi component @ ${VERSION}"
for f in "${FILES[@]}"; do
  curl -fsSL "${BASE}/${f}" -o "${WIFI_DIR}/${f}"
done
echo "${VERSION}" > "${WIFI_DIR}/UPSTREAM_VERSION"

echo "==> Committing pristine vendor import (merge base for the next update)"
git -C "$ROOT" add "${WIFI_DIR}"
git -C "$ROOT" commit -q -m "vendor: import pristine esphome wifi ${VERSION}"

echo "==> Re-applying marsrelay patch with 3-way merge"
if git -C "$ROOT" apply --3way "$PATCH"; then
  echo "    patch applied cleanly"
else
  cat >&2 <<EOF

!! The patch did not apply cleanly -- components/wifi/wifi_component.cpp now has
!! conflict markers. Resolve them, then finish manually:

     git diff HEAD -- components/wifi/wifi_component.cpp > $PATCH
     # bump esphome_version in .github/workflows/ci.yml and the "Current base"
     # in components/wifi/README.md to ${VERSION}
     scripts/check-wifi-fork.sh
     git add -A && git commit -m "Re-apply marsrelay patch on esphome wifi ${VERSION}"
EOF
  exit 1
fi

echo "==> Regenerating patch against the new base"
# Diff against HEAD (the pristine vendor import just committed), not the index:
# git apply --3way stages its result in modern git, which would make a plain
# `git diff` (worktree vs index) empty and silently produce an empty patch.
git -C "$ROOT" diff HEAD -- components/wifi/wifi_component.cpp > "$PATCH"

echo "==> Bumping version markers"
sed -i -E "s/esphome_version: \"[0-9][0-9.]*\"/esphome_version: \"${VERSION}\"/" \
  "$ROOT/.github/workflows/ci.yml"
sed -i -E "s#tree/[0-9][0-9.]*/esphome/components/wifi#tree/${VERSION}/esphome/components/wifi#" \
  "$WIFI_DIR/README.md"
sed -i -E "s/\*\*Upstream version:\*\* \`[0-9][0-9.]*\`/**Upstream version:** \`${VERSION}\`/" \
  "$WIFI_DIR/README.md"

# Fail loudly if any substitution silently no-op'd (e.g. a marker format drifted),
# which would otherwise leave stale version pins behind.
grep -qF "esphome_version: \"${VERSION}\"" "$ROOT/.github/workflows/ci.yml" \
  || { echo "ERROR: esphome_version not bumped in ci.yml" >&2; exit 1; }
grep -qF "tree/${VERSION}/esphome/components/wifi" "$WIFI_DIR/README.md" \
  || { echo "ERROR: upstream tree URL not bumped in README.md" >&2; exit 1; }
grep -qF "**Upstream version:** \`${VERSION}\`" "$WIFI_DIR/README.md" \
  || { echo "ERROR: Upstream version not bumped in README.md" >&2; exit 1; }

echo "==> Verifying the result equals pristine upstream + the patch"
"$ROOT/scripts/check-wifi-fork.sh"

cat <<EOF

Done. Review the staged changes, then commit the re-apply:

    git add -A && git commit -m "Re-apply marsrelay patch on esphome wifi ${VERSION}"

Then validate the build:

    esphome config marsrelay_esp32s3.yaml
EOF
