#!/bin/bash
# build-appimage.sh — Linux AppImage build (D7-era base, hardened by F19).
#
# F19 changes vs the legacy version:
#   * linuxdeploy + qt plugin are PINNED to fixed release tags and verified
#     against packaging/appimage-tool-checksums.txt through
#     packaging/appimage_toolchain.sh — the old '-continuous' download made
#     builds non-reproducible and unverifiable. The build fails closed while
#     hashes are PENDING; a maintainer records them once (see the checksums
#     file header), after which downloads and --tools-dir provisioning are
#     both hash-verified.
#   * the full PROJ share ships dereferenced (cp -RL), not just proj.db —
#     missing grid files otherwise push coordinate ops toward network
#     fallbacks the offline contract forbids.
#   * the AppDir payload gets a canonical /2 manifest (components + compat),
#     verified through scripts/verify_bundle_manifest.py before packaging —
#     the same verify authority as the offline lab bundle.
#   * build parallelism defaults to 2 (repo envelope; override with
#     SICNU_BUILD_PARALLEL_LEVEL), never nproc.
#
# usage: build-appimage.sh [--tools-dir DIR] [--max-mb N]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-appimage"
APPDIR="$BUILD_DIR/AppDir"

# shellcheck source=appimage_toolchain.sh
source "$SCRIPT_DIR/appimage_toolchain.sh"

LINUXDEPLOY_TAG="1-alpha-20240109"
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_TAG}/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/${LINUXDEPLOY_TAG}/linuxdeploy-plugin-qt-x86_64.AppImage"
MAX_MB=900

usage() { sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
while [ $# -gt 0 ]; do
  case "$1" in
    --tools-dir) [ $# -ge 2 ] || usage; export APPIMAGE_TOOLS_DIR=$2; shift 2 ;;
    --max-mb)    [ $# -ge 2 ] || usage; MAX_MB=$2; shift 2 ;;
    -h|--help)   usage ;;
    *) echo "build-appimage: unknown option: $1" >&2; exit 2 ;;
  esac
done

echo "=== Building SICNU GEO RS AppImage ==="

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"
cmake "$PROJECT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DENABLE_TESTS=OFF
make -j"${SICNU_BUILD_PARALLEL_LEVEL:-2}"

make install DESTDIR="$APPDIR"

# Copy resources
mkdir -p "$APPDIR/usr/share/sicnu_geo_rs/resources"
cp -r "$PROJECT_DIR/resources/fonts" "$APPDIR/usr/share/sicnu_geo_rs/resources/"
cp -r "$PROJECT_DIR/resources/icons" "$APPDIR/usr/share/sicnu_geo_rs/resources/"
cp "$PROJECT_DIR/resources/styles.qss" "$APPDIR/usr/share/sicnu_geo_rs/resources/"

mkdir -p "$APPDIR/usr/share/sicnu_geo_rs/qgis_ref/resources"
for _sym in \
  "$PROJECT_DIR/refs/qgis/resources/symbology-style.xml" \
  "$PROJECT_DIR/qgis_ref/resources/symbology-style.xml"
do
  if [ -f "$_sym" ]; then
    cp "$_sym" "$APPDIR/usr/share/sicnu_geo_rs/qgis_ref/resources/"
    break
  fi
done

# Bundle OTB binaries (required — OTB is part of the application)
OTB_SOURCE="${OTB_INSTALL_DIR:-/opt/otb}"
if [ -d "$OTB_SOURCE/bin" ]; then
    echo "=== Bundling OTB from $OTB_SOURCE ==="
    mkdir -p "$APPDIR/usr/tools/otb"
    cp -a "$OTB_SOURCE/bin/otbcli_"* "$APPDIR/usr/tools/otb/" 2>/dev/null || true
    cp -a "$OTB_SOURCE/bin/otbcli" "$APPDIR/usr/tools/otb/" 2>/dev/null || true
    # Copy OTB libraries
    mkdir -p "$APPDIR/usr/lib/otb"
    cp -a "$OTB_SOURCE/lib/"*.so* "$APPDIR/usr/lib/otb/" 2>/dev/null || true
    # Copy OTB share (applications metadata)
    mkdir -p "$APPDIR/usr/share/otb"
    cp -a "$OTB_SOURCE/share/otb/"* "$APPDIR/usr/share/otb/" 2>/dev/null || true
    echo "OTB bundled successfully"
else
    echo "WARNING: OTB not found at $OTB_SOURCE. Set OTB_INSTALL_DIR to bundle OTB."
    echo "  Example: OTB_INSTALL_DIR=/path/to/otb $0"
fi

# Bundle GDAL tools (optional — system GDAL is used by default)
GDAL_SOURCE="${GDAL_INSTALL_DIR:-}"
if [ -n "$GDAL_SOURCE" ] && [ -d "$GDAL_SOURCE/bin" ]; then
    echo "=== Bundling GDAL tools from $GDAL_SOURCE ==="
    mkdir -p "$APPDIR/usr/tools/gdal"
    for tool in gdal_translate gdalwarp gdalinfo gdalbuildvrt gdaldem gdal_grid gdal_rasterize ogr2ogr ogrinfo; do
        cp "$GDAL_SOURCE/bin/$tool" "$APPDIR/usr/tools/gdal/" 2>/dev/null || true
    done
    echo "GDAL tools bundled"
fi

# Bundle PROJ data (F19: full share, dereferenced) for offline CRS transforms.
mkdir -p "$APPDIR/usr/share/proj"
proj_share=""
for c in /usr/share/proj /usr/local/share/proj; do
  [ -f "$c/proj.db" ] && proj_share=$c && break
done
if [ -z "$proj_share" ] && command -v gdal-config >/dev/null 2>&1; then
  gdal_data="$(gdal-config --datadir 2>/dev/null || true)"
  [ -f "${gdal_data%/gdal}/proj/proj.db" ] && proj_share="${gdal_data%/gdal}/proj"
fi
if [ -n "$proj_share" ]; then
  cp -RL "$proj_share/." "$APPDIR/usr/share/proj/"
  [ -f "$APPDIR/usr/share/proj/proj.db" ] \
    || { echo "build-appimage: proj.db missing after copy" >&2; exit 1; }
else
  echo "build-appimage: FATAL: proj share not found — install proj-data" >&2
  exit 1
fi

# F19: canonical payload manifest — same schema family as the offline lab
# bundle, verified by the same canonical verifier before packaging. NOTE: the
# manifest is a PRE-PACKAGING snapshot of the DESTDIR payload (usr/ prefix,
# required accordingly); linuxdeploy later adds AppRun/scaffolding that is not
# in the manifest — the shipped AppImage is NOT self-verifying by design
# (unlike the offline lab bundle, which ships a verifier covering itself).
echo "=== writing + verifying AppDir payload manifest ==="
gdal_ver="$(gdal-config --version 2>/dev/null || true)"
proj_ver="$(pkg-config --modversion proj 2>/dev/null || true)"
python3 - "$APPDIR" "$MAX_MB" "$gdal_ver" "$proj_ver" <<'PY'
import hashlib, json, os, sys, datetime
appdir, ceiling, gdal_ver, proj_ver = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
files = []
for base, _dirs, names in os.walk(appdir):
    for n in names:
        p = os.path.join(base, n)
        rel = os.path.relpath(p, appdir).replace(os.sep, "/")
        if rel == "manifest.json":
            continue
        files.append({"path": rel,
                      "bytes": os.path.getsize(p),
                      "sha256": hashlib.sha256(open(p, "rb").read()).hexdigest()})
files.sort(key=lambda f: f["path"])
components = {}
if gdal_ver:
    components["gdal"] = {"version": gdal_ver, "source": "host"}
if proj_ver:
    components["proj"] = {"version": proj_ver, "source": "host"}
manifest = {
    "schema": "sicnu.offline_bundle/2",
    "bundle_version": "appimage-payload",
    "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
    "size_ceiling_mb": ceiling,
    "required": ["usr/bin/", "usr/share/proj/", "usr/share/sicnu_geo_rs/"],
    "files": files,
    "components": components,
    "compat": {"min_reader_schema": 1, "bundle_kind": "appimage-payload"},
}
with open(os.path.join(appdir, "manifest.json"), "w", encoding="utf-8") as fh:
    json.dump(manifest, fh, ensure_ascii=False, indent=2)
    fh.write("\n")
print(f"payload manifest: {len(files)} files")
PY
python3 "$PROJECT_DIR/scripts/verify_bundle_manifest.py" "$APPDIR"

# Toolchain: pinned downloads (or --tools-dir pre-provision), hash-verified
# through packaging/appimage_toolchain.sh — fails closed on PENDING hashes.
LINUXDEPLOY="$BUILD_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$BUILD_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
fetch_tool "$LINUXDEPLOY" "$LINUXDEPLOY_URL" "linuxdeploy-x86_64.AppImage"
fetch_tool "$LINUXDEPLOY_QT" "$LINUXDEPLOY_QT_URL" "linuxdeploy-plugin-qt-x86_64.AppImage"

cd "$BUILD_DIR"
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$PROJECT_DIR/packaging/sicnu_geo_rs.desktop" \
    --icon-file "$PROJECT_DIR/packaging/sicnu_geo_rs.svg" \
    --plugin qt \
    --output appimage

echo "=== AppImage created in $BUILD_DIR ==="
ls -lh "$BUILD_DIR"/*.AppImage
