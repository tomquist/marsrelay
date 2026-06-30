#!/usr/bin/env bash
#
# Shared helpers for the vendored ESPHome `wifi` component scripts. Source this
# file; do not execute it.
#
# It centralises how we talk to upstream so the component's file set is
# *discovered from the upstream folder* rather than hardcoded in each script.
# That means an upstream file being added, removed, or renamed is picked up
# automatically instead of silently slipping past a stale list.

# Path of the wifi component within the esphome repo.
WIFI_UPSTREAM_PATH="esphome/components/wifi"

# Paths under components/wifi/ that are *ours*, not vendored from upstream:
# UPSTREAM_VERSION, this fork's README, and the patches/ directory. Used to tell
# vendored upstream files apart from our own when syncing or verifying.
wifi_is_local_path() {  # wifi_is_local_path <path-relative-to-component-dir>
  case "$1" in
    README.md | UPSTREAM_VERSION | patches | patches/*) return 0 ;;
    *) return 1 ;;
  esac
}

# curl with retry/timeout defaults so the unattended daily workflow can ride out
# a transient network blip instead of surfacing it as a "manual update" failure.
_wifi_curl() {  # _wifi_curl <curl-args...>
  curl -fsSL --retry 3 --retry-delay 2 --retry-connrefused --max-time 60 "$@"
}

# curl against the GitHub API, attaching a token when one is in the environment
# so the directory listing isn't throttled by the unauthenticated rate limit.
_wifi_api_curl() {  # _wifi_api_curl <url>
  local token="${GH_TOKEN:-${GITHUB_TOKEN:-}}"
  if [[ -n "$token" ]]; then
    _wifi_curl -H "Authorization: Bearer ${token}" \
      -H "Accept: application/vnd.github+json" "$1"
  else
    _wifi_curl -H "Accept: application/vnd.github+json" "$1"
  fi
}

# List the component's files at a git ref, one path (relative to the component
# dir) per line, by walking the upstream directory and recursing into any
# subdirectories. Output is sorted for stable diffing/comparison.
wifi_upstream_files() {  # wifi_upstream_files <ref>
  _wifi_upstream_walk "$1" "" | sort
}

_wifi_upstream_walk() {  # _wifi_upstream_walk <ref> <subdir>
  local ref="$1" sub="${2:-}"
  local url="https://api.github.com/repos/esphome/esphome/contents/${WIFI_UPSTREAM_PATH}${sub:+/$sub}?ref=${ref}"
  local entries etype name rel
  entries="$(_wifi_api_curl "$url" | python3 -c '
import json, sys
for e in json.load(sys.stdin):
    print(e["type"] + "\t" + e["name"])
')"
  while IFS=$'\t' read -r etype name; do
    [[ -n "$name" ]] || continue
    rel="${sub:+$sub/}$name"
    if [[ "$etype" == "dir" ]]; then
      _wifi_upstream_walk "$ref" "$rel"
    else
      printf '%s\n' "$rel"
    fi
  done <<< "$entries"
}

# Download the whole component at a ref into <dest> (folder-based), preserving
# any subdirectory structure. Files whose names collide with ours (README.md,
# UPSTREAM_VERSION, patches/) are skipped so a future upstream file can never
# clobber the fork's own when <dest> is the live components/wifi dir; such a
# collision still surfaces as drift in check-wifi-fork.sh (which lists the full
# upstream set), it just isn't silently written over here.
wifi_download_upstream() {  # wifi_download_upstream <ref> <dest>
  local ref="$1" dest="$2" rel
  local base="https://raw.githubusercontent.com/esphome/esphome/${ref}/${WIFI_UPSTREAM_PATH}"
  while IFS= read -r rel; do
    wifi_is_local_path "$rel" && continue
    mkdir -p "$dest/$(dirname "$rel")"
    _wifi_curl "${base}/${rel}" -o "$dest/$rel"
  done < <(wifi_upstream_files "$ref")
}

# List the tracked vendored upstream files (paths relative to the component dir),
# i.e. everything git tracks under components/wifi/ except our own files. Used to
# detect drift against the upstream set. <root> is the repo toplevel.
wifi_vendored_files() {  # wifi_vendored_files <root>
  local root="$1" rel
  while IFS= read -r rel; do
    rel="${rel#components/wifi/}"
    wifi_is_local_path "$rel" && continue
    printf '%s\n' "$rel"
  done < <(git -C "$root" ls-files components/wifi) | sort
}
