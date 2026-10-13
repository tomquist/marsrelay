#!/usr/bin/env bash
#
# Write marsrelay_esp32s3.local.yaml: the shipped config with its components
# source pointed at this checkout's components/ instead of GitHub.
#
# marsrelay_esp32s3.yaml fetches github://tomquist/marsrelay@main, which is what
# users want but means validating it locally tests main, not your working tree.
# Validate the generated copy instead:
#
#     scripts/local-config.sh && esphome config marsrelay_esp32s3.local.yaml
#
# The copy is written next to the original so ./components resolves, and is
# gitignored.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
SRC="$ROOT/marsrelay_esp32s3.yaml"
OUT="$ROOT/marsrelay_esp32s3.local.yaml"
REMOTE='source: github://tomquist/marsrelay@main'

grep -qF "$REMOTE" "$SRC" \
  || { echo "ERROR: '$REMOTE' not found in $SRC" >&2; exit 1; }

sed "s|$REMOTE|source: {type: local, path: ./components}|" "$SRC" > "$OUT"
echo "Wrote $OUT"
