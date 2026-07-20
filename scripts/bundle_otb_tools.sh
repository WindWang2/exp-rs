#!/usr/bin/env bash
# bundle_otb_tools.sh — Copy built OTB CLI launchers into build/tools/otb/
set -euo pipefail

BUILD_DIR="${1:?build dir required}"
SRC_DIR="${2:?source dir required}"
BUNDLE_DIR="${BUILD_DIR}/tools/otb"

mkdir -p "${BUNDLE_DIR}/bin" "${BUNDLE_DIR}/lib/otb/applications" "${BUNDLE_DIR}/lib"

if [[ -d "${BUILD_DIR}/lib/otb/applications" ]]; then
    cp -a "${BUILD_DIR}/lib/otb/applications/." "${BUNDLE_DIR}/lib/otb/applications/"
fi

# Shared runtime libs: vendored deps + OTB shared modules (when BUILD_SHARED_LIBS=ON)
shopt -s nullglob
for lib in \
    "${BUILD_DIR}"/lib/libmuparserx.so* \
    "${BUILD_DIR}"/lib/libsvm.so* \
    "${BUILD_DIR}"/lib/libOTB*.so* \
    "${BUILD_DIR}"/lib/libotb*.so*
do
    cp -a "${lib}" "${BUNDLE_DIR}/lib/" 2>/dev/null || true
done
shopt -u nullglob

# Ensure bundled env puts OTB shared libs on the loader path
# (appended after profile is written below — see LD_LIBRARY_PATH export)

# Prefer hard copies; fall back to ln -sfn when src and dest are the same inode
# (re-running the bundle on an already-staged tree).
_sicnu_stage() {
    local src="$1" dest="$2"
    if [[ ! -e "$src" ]]; then return 0; fi
    if [[ -e "$dest" ]] && [[ "$(readlink -f "$src")" == "$(readlink -f "$dest")" ]]; then
        return 0
    fi
    cp -a "$src" "$dest" 2>/dev/null || ln -sfn "$src" "$dest"
}

if [[ -f "${BUILD_DIR}/bin/otbApplicationLauncherCommandLine" ]]; then
    _sicnu_stage "${BUILD_DIR}/bin/otbApplicationLauncherCommandLine" \
        "${BUNDLE_DIR}/bin/otbApplicationLauncherCommandLine"
fi

if [[ -f "${BUILD_DIR}/bin/otbcli" ]]; then
    _sicnu_stage "${BUILD_DIR}/bin/otbcli" "${BUNDLE_DIR}/bin/otbcli"
fi

shopt -s nullglob
for script in "${BUILD_DIR}"/bin/otbcli_*; do
    _sicnu_stage "${script}" "${BUNDLE_DIR}/bin/$(basename "${script}")"
done
shopt -u nullglob

# Runtime env for bundled OTB (sourced by bin/otbcli)
cat > "${BUNDLE_DIR}/otbenv.profile" <<'EOF'
# SICNU GEO RS — bundled OTB environment (auto-generated)
cat_path()
{
  if [ $# -eq 0 ]; then exit 0; fi
  if [ $# -eq 1 ]; then echo "$1"; exit 0; fi
  cur="$1"
  shift 1
  next=$(cat_path "$@")
  if [ -z "$cur" ]; then echo "$next"
  elif [ -z "$next" ]; then echo "$cur"
  else echo "$cur:$next"
  fi
}

if [ -n "${BASH_SOURCE[0]:-}" ]; then
  OTB_INSTALL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
else
  OTB_INSTALL_DIR="$(cd "$(dirname "$0")" && pwd)"
fi

OTB_APPLICATION_PATH=$(cat_path "$OTB_INSTALL_DIR/lib/otb/applications" "${OTB_APPLICATION_PATH:-}")
PATH=$(cat_path "$OTB_INSTALL_DIR/bin" "$PATH")
LD_LIBRARY_PATH=$(cat_path "$OTB_INSTALL_DIR/lib" "${LD_LIBRARY_PATH:-}")
LC_NUMERIC=C

if [ -z "${GDAL_DATA:-}" ] && command -v gdal-config >/dev/null 2>&1; then
  GDAL_DATA="$(gdal-config --datadir)"
fi

export OTB_APPLICATION_PATH PATH LD_LIBRARY_PATH LC_NUMERIC GDAL_DATA
EOF

echo "Bundled OTB tools -> ${BUNDLE_DIR} ($(ls -1 "${BUNDLE_DIR}/bin" 2>/dev/null | wc -l) launchers)"