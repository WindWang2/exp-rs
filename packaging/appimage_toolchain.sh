#!/usr/bin/env bash
# appimage_toolchain.sh — pinned toolchain provisioning for build-appimage.sh
# (deployment-packaging-11/F19).
#
# fetch_tool <dest> <url-tag-verified-url> <filename>
#
# Contract:
#   * every tool is PINNED to a fixed release tag (no -continuous downloads —
#     those made builds non-reproducible and unverifiable);
#   * every artifact is verified against the SHA256 recorded in the
#     checksums file (packaging/appimage-tool-checksums.txt, overridable via
#     APPIMAGE_TOOL_CHECKSUMS for tests);
#   * a checksum entry that is missing or still the PENDING placeholder is a
#     hard error with the exact one-command remedy — fail closed, never ship
#     an unverified bootstrap artifact;
#   * --tools-dir/APPIMAGE_TOOLS_DIR pre-provisioning is supported: the local
#     copy is verified against the same checksums file (offline reproducible
#     once a maintainer has recorded the hashes once).
#
# This file is sourced by build-appimage.sh and unit-tested directly
# (tests run fetch_tool against fixture files) — keep it free of side
# effects on source.

APPIMAGE_TOOL_CHECKSUMS_DEFAULT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/appimage-tool-checksums.txt"

fetch_tool() {
  local dest=$1 url=$2 filename=$3
  local checksums="${APPIMAGE_TOOL_CHECKSUMS:-$APPIMAGE_TOOL_CHECKSUMS_DEFAULT}"
  local want

  if [ ! -f "$checksums" ]; then
    echo "appimage_toolchain: checksums file missing: $checksums" >&2
    return 1
  fi
  want="$(grep -E "^[0-9a-f]{64}[[:space:]]+${filename}[[:space:]]*$" "$checksums" | head -1 | cut -d' ' -f1)"
  if [ -z "$want" ]; then
    if grep -qE "(^[[:space:]]*)?PENDING[[:space:]]+${filename}[[:space:]]*$" "$checksums"; then
      echo "appimage_toolchain: no SHA256 recorded for $filename yet." >&2
      echo "  Record it once from the pinned release artifact:" >&2
      echo "    curl -fL -o $filename '$url' && sha256sum $filename >> $checksums" >&2
      echo "  (or provision --tools-dir and record the same hash)" >&2
    else
      echo "appimage_toolchain: $filename not listed in $checksums" >&2
    fi
    return 1
  fi

  local cached=""
  if [ -n "${APPIMAGE_TOOLS_DIR:-}" ]; then
    cached="$APPIMAGE_TOOLS_DIR/$filename"
    if [ ! -f "$cached" ]; then
      echo "appimage_toolchain: pre-fetched tool missing: $cached" >&2
      return 1
    fi
    echo "=== verifying pre-fetched $filename ==="
    if ! echo "$want  $cached" | sha256sum -c - >/dev/null 2>&1; then
      echo "appimage_toolchain: SHA256 mismatch for pre-fetched $filename" >&2
      return 1
    fi
    cp "$cached" "$dest"
  else
    echo "=== downloading $filename (pinned) ==="
    if ! curl -fL -o "$dest" "$url"; then
      echo "appimage_toolchain: download failed: $url" >&2
      rm -f "$dest"
      return 1
    fi
    if ! echo "$want  $dest" | sha256sum -c - >/dev/null 2>&1; then
      echo "appimage_toolchain: SHA256 mismatch for downloaded $filename" >&2
      rm -f "$dest"
      return 1
    fi
  fi
  chmod +x "$dest"
  return 0
}
