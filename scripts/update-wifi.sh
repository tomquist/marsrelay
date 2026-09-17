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
#      commits it as a "vendor import", so the re-apply below shows up as an
#      ordinary diff against pristine upstream.
#   2. Re-creates the patch's merge base (pristine wifi_component.cpp at the
#      *previous* pin) as a git blob and re-applies
#      components/wifi/patches/marsrelay-ap-sta.patch with `git apply --3way`.
#      Where upstream rewrote a patched region you get standard git conflict
#      markers instead of a silent mis-apply. The blob is fetched from upstream
#      rather than read out of history, because history does not reliably have
#      it: the update PRs are squash-merged, which drops the vendor import.
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
. "$(dirname "${BASH_SOURCE[0]}")/lib-wifi.sh"

WIFI_DIR="$ROOT/components/wifi"
PATCH="$WIFI_DIR/patches/marsrelay-ap-sta.patch"

# The version we are updating *from*, read before UPSTREAM_VERSION is overwritten
# below. It identifies the patch's merge base; see the blob step further down.
PREV_VERSION="$(cat "${WIFI_DIR}/UPSTREAM_VERSION")"

# The component's file set is discovered from the upstream folder (see
# scripts/lib-wifi.sh); everything else under components/wifi/ (README.md,
# UPSTREAM_VERSION, patches/) is ours and is never overwritten.

# Require a clean components/wifi tree so the vendor-import commit below captures
# only the freshly downloaded upstream files (git add stages all of WIFI_DIR).
# Use status --porcelain (not diff-index) so stat-only changes don't false-abort.
if [[ -n "$(git -C "$ROOT" status --porcelain -- "$WIFI_DIR")" ]]; then
  echo "ERROR: $WIFI_DIR has uncommitted changes; commit or stash them first." >&2
  exit 1
fi

echo "==> Downloading pristine esphome wifi component @ ${VERSION}"
# Drop the previously vendored upstream files first so an upstream rename or
# removal is reflected (our own files are left untouched), then fetch the folder.
while IFS= read -r rel; do
  git -C "$ROOT" rm -q -- "components/wifi/${rel}"
done < <(wifi_vendored_files "$ROOT")
wifi_download_upstream "$VERSION" "$WIFI_DIR"
echo "${VERSION}" > "${WIFI_DIR}/UPSTREAM_VERSION"

echo "==> Committing pristine vendor import"
git -C "$ROOT" add -A -- "${WIFI_DIR}"
git -C "$ROOT" commit -q -m "vendor: import pristine esphome wifi ${VERSION}"

# `git apply --3way` needs the patch's *pre-image* blob -- the left-hand side of
# its `index <old>..<new>` header, i.e. wifi_component.cpp as pristine upstream
# had it at PREV_VERSION -- to be present in the object database. The vendor
# import above puts that blob in history, but only on this branch: the update PRs
# are squash-merged, so on the default branch the blob is gone and --3way silently
# degrades to a straight `git apply` ("repository lacks the necessary blob").
# That fallback applies cleanly whenever upstream left the patched regions'
# context alone, which is why it went unnoticed -- but when it does fail it leaves
# the file untouched, with no conflict markers to resolve.
#
# So re-create the blob from upstream instead of relying on history. It is
# content-addressed: the download either hashes to what the patch records or it
# does not, and a mismatch just means no merge base -- the behaviour we already
# had -- rather than a wrong one.
echo "==> Materialising the patch's merge base (pristine ${PREV_VERSION})"
have_merge_base=0
base_blob_want="$(sed -n 's/^index \([0-9a-f]\{4,\}\)\.\..*/\1/p' "$PATCH" | head -n1)"
base_tmp="$(mktemp)"
if [[ -z "$base_blob_want" ]]; then
  echo "    WARNING: $(basename "$PATCH") has no 'index' header; no merge base." >&2
elif ! wifi_download_upstream_file "$PREV_VERSION" wifi_component.cpp "$base_tmp"; then
  echo "    WARNING: could not download pristine ${PREV_VERSION} wifi_component.cpp;" >&2
  echo "    continuing without a merge base." >&2
else
  base_blob_got="$(git -C "$ROOT" hash-object -w "$base_tmp")"
  if [[ "$base_blob_got" == "$base_blob_want"* ]]; then
    have_merge_base=1
    echo "    merge base ${base_blob_want} available"
  else
    # The recorded patch is not a diff against pristine PREV_VERSION. Worth
    # knowing about, but check-wifi-fork.sh is the check that adjudicates it.
    echo "    WARNING: pristine ${PREV_VERSION} wifi_component.cpp hashes to" >&2
    echo "    ${base_blob_got}, but $(basename "$PATCH") records ${base_blob_want}" >&2
    echo "    as its base; continuing without a merge base." >&2
  fi
fi
rm -f "$base_tmp"

if [[ $have_merge_base -eq 1 ]]; then
  echo "==> Re-applying marsrelay patch with 3-way merge"
else
  echo "==> Re-applying marsrelay patch (no merge base; direct apply only)"
fi
if git -C "$ROOT" apply --3way "$PATCH"; then
  echo "    patch applied cleanly"
elif grep -q '^<<<<<<< ' "${WIFI_DIR}/wifi_component.cpp"; then
  cat >&2 <<EOF

!! The patch did not apply cleanly -- components/wifi/wifi_component.cpp now has
!! conflict markers. Resolve them, then finish manually:

     \$EDITOR components/wifi/wifi_component.cpp   # resolve the markers
     git add components/wifi/wifi_component.cpp    # clears the conflicted index
     git diff HEAD -- components/wifi/wifi_component.cpp > $PATCH
     # bump the "Current base" / Upstream version in components/wifi/README.md
     # to ${VERSION} (UPSTREAM_VERSION and the CI pin are already handled)
     scripts/check-wifi-fork.sh
     git add -A && git commit -m "Re-apply marsrelay patch on esphome wifi ${VERSION}"
EOF
  exit 1
else
  # No merge base, so git could only try a straight apply and that failed:
  # wifi_component.cpp is pristine ${VERSION}, unchanged. Do NOT regenerate the
  # patch from it -- the diff would be empty and the fork's delta would be lost.
  cat >&2 <<EOF

!! The patch did not apply and there was no merge base to fall back on, so
!! components/wifi/wifi_component.cpp is still pristine ${VERSION} -- there is
!! nothing to resolve in place and $(basename "$PATCH") must NOT be regenerated
!! from it.
!!
!! Re-run once the merge base is available (check the WARNING above), or port the
!! patch by hand onto pristine ${VERSION}, keeping its two marsrelay hunks:
!! always start the fallback AP in loop(), and keep the AP up in
!! check_connecting_finished(). Then:
!!
!!     git diff HEAD -- components/wifi/wifi_component.cpp > $PATCH
!!     # bump the "Current base" / Upstream version in components/wifi/README.md
!!     # to ${VERSION} (UPSTREAM_VERSION and the CI pin are already handled)
!!     scripts/check-wifi-fork.sh
!!     git add -A && git commit -m "Re-apply marsrelay patch on esphome wifi ${VERSION}"
EOF
  exit 1
fi

echo "==> Regenerating patch against the new base"
# Diff against HEAD (the pristine vendor import just committed), not the index:
# git apply --3way stages its result in modern git, which would make a plain
# `git diff` (worktree vs index) empty and silently produce an empty patch.
git -C "$ROOT" diff HEAD -- components/wifi/wifi_component.cpp > "$PATCH"

echo "==> Bumping version markers"
# The CI esphome pin is derived from components/wifi/UPSTREAM_VERSION (already
# written above), so the workflow files are intentionally left untouched here --
# that keeps the automated wifi-update-check commit pushable with the default
# GITHUB_TOKEN, which can't modify .github/workflows/ without `workflows` perms.
sed -i -E "s#tree/[0-9][0-9.]*/esphome/components/wifi#tree/${VERSION}/esphome/components/wifi#" \
  "$WIFI_DIR/README.md"
sed -i -E "s/\*\*Upstream version:\*\* \`[0-9][0-9.]*\`/**Upstream version:** \`${VERSION}\`/" \
  "$WIFI_DIR/README.md"

# Fail loudly if any substitution silently no-op'd (e.g. a marker format drifted),
# which would otherwise leave stale version pins behind.
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
