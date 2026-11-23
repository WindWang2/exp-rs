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
# This file is sourced by build-appimage.sh; the fetch_tool contract is
# exercised offline by a fixture matrix (pinned-hash pass, tampered reject,
# PENDING reject, pre-provisioned tools-dir verify). Keep it free of side
# effects on source.

APPIMAGE_TOOL_CHECKSUMS_DEFAULT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/appimage-tool-checksums.txt"

# Extract the runtime from the verified Type 2 tool, preserving its ELF layout
# while clearing the tool's update channel, digest and signature reservations.
extract_appimage_runtime() (
  set -euo pipefail
  local tool=$1 runtime=$2 runtime_size section size offset
  runtime_size=$("$tool" --appimage-offset)
  [[ $runtime_size =~ ^[1-9][0-9]*$ ]] || { echo "build-appimage: invalid runtime offset" >&2; exit 1; }
  head -c "$runtime_size" "$tool" > "$runtime"
  for section in .upd_info .digest_md5 .sha256_sig .sig_key; do
    read -r size offset < <(LC_ALL=C objdump -h -j "$section" "$runtime" |
      awk -v name="$section" '$2 == name { print $3, $6 }') ||
      { echo "build-appimage: missing ELF section $section" >&2; exit 1; }
    dd if=/dev/zero of="$runtime" bs=1 seek="$((16#$offset))" count="$((16#$size))" conv=notrunc status=none
  done
)

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

# linuxdeploy-plugin-qt enumerates every sqldrivers entry and aborts if even
# an optional client's shared library is missing. Give it a private plugin
# directory while reporting the SDK's original paths for all other queries.
prepare_qt_plugins() (
  set -euo pipefail
  local stage=$1 qt6_dir=$2 qmake="" candidate path plugins libs group driver deps
  [[ -d $qt6_dir ]] || { echo "build-appimage: CMake Qt6_DIR is missing: $qt6_dir" >&2; exit 1; }
  local selected_libs="$qt6_dir/../.."
  local candidates=("$selected_libs/../bin/qmake6" "$selected_libs/../bin/qmake" qmake6 qmake)
  if [[ -n ${QMAKE:-} ]]; then
    candidates=("$QMAKE")
  fi
  for candidate in "${candidates[@]}"; do
    path=$(command -v "$candidate") || continue
    libs=$("$path" -query QT_INSTALL_LIBS) || continue
    if [[ $libs -ef $selected_libs && $("$path" -query QT_VERSION) == 6.* ]]; then
      qmake=$path
      break
    fi
  done
  [[ -n $qmake ]] || { echo "build-appimage: no matching Qt 6 qmake for $qt6_dir" >&2; exit 1; }
  plugins=$("$qmake" -query QT_INSTALL_PLUGINS)
  local sqlite="$plugins/sqldrivers/libqsqlite.so"
  [[ -f $sqlite ]] || { echo "build-appimage: required Qt SQLite driver missing: $sqlite" >&2; exit 1; }
  mkdir -p "$stage/plugins/sqldrivers"
  for group in "$plugins"/*; do
    [[ $group == "$plugins/sqldrivers" ]] || ln -s "$group" "$stage/plugins/"
  done
  for driver in "$plugins"/sqldrivers/*.so; do
    # Match the Qt library search path used by the pinned deployer. ldd may
    # exit successfully when a transitive dependency reports "not found".
    if deps=$(LC_ALL=C LD_LIBRARY_PATH="$libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$driver" 2>&1) &&
       [[ $deps != *"not found"* ]]; then
      cp -L "$driver" "$stage/plugins/sqldrivers/"
      continue
    fi
    if [[ $deps == *"not found"* ]]; then
      deps=$(grep -F 'not found' <<< "$deps")
    fi
    if [[ $driver == "$sqlite" ]]; then
      echo "build-appimage: required Qt SQLite driver cannot load: $deps" >&2
      exit 1
    else
      echo "build-appimage: skipping optional ${driver##*/}: $deps" >&2
    fi
  done
  {
    printf '#!/usr/bin/env bash\nset -euo pipefail\nqmake=%q\nplugins=%q\n' "$qmake" "$stage/plugins"
    cat <<'SH'
if [[ $# == 1 && $1 == -query ]]; then
  "$qmake" -query | awk -v plugins="$plugins" '/^QT_INSTALL_PLUGINS:/ { print "QT_INSTALL_PLUGINS:" plugins; next } { print }'
elif [[ $# == 2 && $1 == -query && $2 == QT_INSTALL_PLUGINS ]]; then
  printf '%s\n' "$plugins"
else
  exec "$qmake" "$@"
fi
SH
  } > "$stage/qmake"
  chmod +x "$stage/qmake"
)
