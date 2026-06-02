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

mkdir -p "$APPDIR/usr/share/sicnu_geo_rs/resources"
cp -r "$PROJECT_DIR/resources/fonts" "$APPDIR/usr/share/sicnu_geo_rs/resources/"
cp -r "$PROJECT_DIR/resources/icons" "$APPDIR/usr/share/sicnu_geo_rs/resources/"
cp "$PROJECT_DIR/resources/styles.qss" "$APPDIR/usr/share/sicnu_geo_rs/resources/"

mkdir -p "$APPDIR/usr/share/sicnu_geo_rs/qgis_ref/resources"
cp "$PROJECT_DIR/qgis_ref/resources/symbology-style.xml" \
   "$APPDIR/usr/share/sicnu_geo_rs/qgis_ref/resources/" 2>/dev/null || true

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
