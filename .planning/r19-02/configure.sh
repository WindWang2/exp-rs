#!/bin/sh
# R19-02 configure — replicates the dependency resolution proven by r18-c (build-r18-c cache).
# Vendor prefix: /home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr
# sqlite3/zstd/pcre2-8 are PINNED to system .so: the vendor prefix carries
# non-PIC static archives that break libqgis_core's shared link.
# NOTE: runs cmake DIRECTLY (no nested resource_guard): callers wrap this
# script in the global -j2 lock; nesting the same lock self-deadlocks.
set -e
cd "$(dirname "$0")/../.."   # repo root (this file lives in .planning/r19-02/)
export CMAKE_BUILD_PARALLEL_LEVEL=2
export CMAKE_GENERATOR=Ninja
export QT_QPA_PLATFORM=offscreen
V=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr
L=$V/lib/x86_64-linux-gnu
cmake -S . -B build-r19-02 \
  -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON \
  -DCMAKE_PREFIX_PATH="$L" \
  -DEXPAT_LIBRARY=/usr/lib/libexpat.so -DEXPAT_LIBRARY_RELEASE=/usr/lib/libexpat.so \
  -DGDAL_INCLUDE_DIR="$V/include/gdal" -DGDAL_LIBRARY="$L/libgdal.so" \
  -DGEOS_DIR="$L/cmake/GEOS" -DGEOS_INCLUDE_DIR="$V/include" \
  -DPROJ_DIR="$L/cmake/proj" -DPROJ_INCLUDE_DIR="$V/include" -DPROJ_LIBRARY="$L/libproj.so" \
  -DQt6_DIR=/usr/lib/cmake/Qt6 \
  -DSQLite3_INCLUDE_DIR=/usr/include -DSQLite3_LIBRARY=/usr/lib/libsqlite3.so \
  -DZSTD_INCLUDE_DIR=/usr/include -DZSTD_LIBRARY=/usr/lib/libzstd.so \
  -DPCRE2_8_LIBRARY=/usr/lib/libpcre2-8.so \
  -DZLIB_LIBRARY=/usr/lib/libz.so -DZLIB_LIBRARY_RELEASE=/usr/lib/libz.so \
  -DGSL_INCLUDE_DIR="$V/include" -DGSL_LIBRARY="$L/libgsl.so" -DGSL_LIBRARY_DEBUG="$L/libgsl.so" \
  -DGSL_CBLAS_LIBRARY="$L/libgslcblas.so" -DGSL_CBLAS_LIBRARY_DEBUG="$L/libgslcblas.so" \
  -DNETCDF_INCLUDE_DIR="$V/include" -DNETCDF_LIBRARY="$L/libnetcdf.so" \
  -DQCA_INCLUDE_DIR=/usr/include/qt6/Qca-qt6/QtCrypto -DQCA_LIBRARY="$L/libqca-qt6.so" \
  -DCURL_LIBRARY_RELEASE="$L/libcurl.so" \
  -DOpenCV_DIR=/usr/lib/cmake/opencv5 \
  -DCMAKE_EXE_LINKER_FLAGS="-L$PWD/build-r19-02/link-shim -Wl,--allow-shlib-undefined" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L$PWD/build-r19-02/link-shim" \
  -DCMAKE_MODULE_LINKER_FLAGS="-L$PWD/build-r19-02/link-shim" \
  "$@"
