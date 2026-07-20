#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-appimage"
APPDIR="$BUILD_DIR/AppDir"

echo "=== Building SICNU GEO RS AppImage ==="

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"
cmake "$PROJECT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DENABLE_TESTS=OFF
make -j"$(nproc)"

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

LINUXDEPLOY="$BUILD_DIR/linuxdeploy-x86_64.AppImage"
if [ ! -f "$LINUXDEPLOY" ]; then
    echo "Downloading linuxdeploy..."
    curl -L -o "$LINUXDEPLOY" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x "$LINUXDEPLOY"
fi

cd "$BUILD_DIR"
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$PROJECT_DIR/packaging/sicnu_geo_rs.desktop" \
    --icon-file "$PROJECT_DIR/packaging/sicnu_geo_rs.svg" \
    --output appimage

echo "=== AppImage created in $BUILD_DIR ==="
ls -lh "$BUILD_DIR"/*.AppImage
