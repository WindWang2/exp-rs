#!/usr/bin/env bash
# r21-A test runner.
#
# Link-time facts on this host:
#   * The build links the SYSTEM Qt 6.12 (/usr/lib).
#   * The vendored GDAL (paleo prefix) needs geos 3.14.1 + ~20 more transitive
#     libs (geotiff/netcdf/hdf5/kml/xerces/poppler/jxl/...), all of which live
#     in that prefix; its own Qt is 6.10 and must NOT be loaded (Qt private ABI
#     mismatch) — hence /tmp/r21a-libs, a SONAME symlink shim holding only the
#     non-Qt deps.
#   * The vendored libgdal references flang wrapper2_* symbols no library on
#     this host provides; /tmp/r21a-deb/libwrapper2stub.so supplies them (the
#     code paths our tests exercise never call them).
#   * GDAL's armadillo-backed solver needs libarmadillo/libarpack/libsuperlu
#     from the seismic vendor prefix (they pull in Qt 6.10 — so they are only
#     reachable through the shim's NEEDED resolution at load, not LD_PRELOAD).
set -euo pipefail
WT="/home/kevin/project/exp-rs-r21-A-20261023T184835Z"
VENDOR="/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr/lib/x86_64-linux-gnu"
SEISMIC_LIB="/home/kevin/project/pw-seismic-runtime-closure-20260930-215353/vendor/prefix/usr/lib"

export QT_QPA_PLATFORM=offscreen
# PROJ's proj.db is not installed system-wide on this host; the seismic vendor
# prefix carries a complete one (needed by CRS-parsing tests).
export PROJ_LIB="/home/kevin/project/pw-seismic-runtime-closure-20260930-215353/vendor/prefix/usr/share/proj"
export LD_LIBRARY_PATH="/tmp/r21a-libs:$SEISMIC_LIB:/tmp/r21a-deb"

# The shim (see above) plus these three seismic solver libs, which libgdal.so
# declares but which are absent from the system. Symlinks only — loading them
# does not drag Qt in (LD_LIBRARY_PATH order puts them after the shim and the
# loader only resolves what is missing).
for extra in libarmadillo.so.15 libarpack.so.2.1.0 libsuperlu.so.7; do
    [ -e "/tmp/r21a-libs/$extra" ] || ln -s "$SEISMIC_LIB/${extra%%.*}.so"* "/tmp/r21a-libs/$extra" 2>/dev/null || \
    ln -sf "$(ls $SEISMIC_LIB/${extra%%.*}* 2>/dev/null | head -1)" "/tmp/r21a-libs/$extra" 2>/dev/null || true
done
for extra in libarpack.so.2.1.0 libsuperlu.so.7; do
    [ -e "/tmp/r21a-libs/$extra" ] || ln -sf "$(ls $SEISMIC_LIB/../lib/x86_64-linux-gnu/${extra%%.*}* 2>/dev/null | head -1)" "/tmp/r21a-libs/$extra" 2>/dev/null || true
done

cd "$WT/build-r21-A/tests"
if [ $# -eq 0 ]; then
    echo "usage: run-tests.sh <test-binary> [catch2 args...]" >&2
    exit 64
fi
exec "./$1" "${@:2}"
