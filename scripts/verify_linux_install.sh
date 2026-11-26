#!/usr/bin/env bash
# Ordinary Linux install smoke, NOT a self-contained Qt deployment.
# Only the external Qt SDK may augment the loader search path: project libraries
# must resolve from the installed prefix through their relative INSTALL_RPATH.
set -euo pipefail

if [[ $# -ne 2 || -z "$1" || -z "$2" ]]; then
  echo "Usage: $0 <installed-prefix> <non-empty-qt-sdk-prefix>" >&2
  exit 2
fi

install_prefix=$(cd -- "$1" && pwd -P)
qt_lib_dir=$(cd -- "$2/lib" && pwd -P)
for library in libQt6Core.so.6 libQt6UiTools.so.6; do
  if [[ ! -f "$qt_lib_dir/$library" ]]; then
    echo "Missing external Qt SDK runtime dependency: $qt_lib_dir/$library" >&2
    exit 1
  fi
done
for executable in sicnu_geo_rs_cli sicnu_worker; do
  if [[ ! -x "$install_prefix/bin/$executable" ]]; then
    echo "Missing installed executable: $install_prefix/bin/$executable" >&2
    exit 1
  fi
done

# Do not append inherited LD_LIBRARY_PATH: it could hide missing install rules
# by finding libsicnu_*.so in a build directory. Preserve the CLI's exit status.
LD_LIBRARY_PATH="$qt_lib_dir" QT_QPA_PLATFORM=offscreen \
  "$install_prefix/bin/sicnu_geo_rs_cli" --help
