#!/usr/bin/env bash
# Runtime environment for running / building the exp-rs verification lanes.
# Usage:  source tools/verification12/env.sh  [build-dir]
#
# Rationale (recorded in GOAL-LOOP-LEDGER-12.md R0.3): the test executables
# link against Qt + vcpkg(debug) + qca + keychain DLLs, so a bare
# `./test.exe` fails with "error while loading shared libraries". The
# bash tool also loses `/usr/bin` from PATH between invocations, which
# breaks coreutils (ls/head/tail/grep) and git's helper scripts.

export PATH="/c/Program Files/Git/usr/bin:/c/Program Files/Git/cmd:$PATH"

_EXPRS_DEPS="/c/deps/Qt/6.8.0/msvc2022_64/bin"
_EXPRS_DEPS="$_EXPRS_DEPS:/c/deps/vcpkg/installed/x64-windows/debug/bin"
_EXPRS_DEPS="$_EXPRS_DEPS:/c/deps/vcpkg/installed/x64-windows/bin"
_EXPRS_DEPS="$_EXPRS_DEPS:/c/deps/qca-install/bin"
_EXPRS_DEPS="$_EXPRS_DEPS:/c/deps/kc-install/bin"

# Optional build dir (defaults to the worktree's build-v12).
_EXPRS_BUILD="${1:-$(pwd)/build-v12}"
_EXPRS_BUILD="${_EXPRS_BUILD//\\//}"

export PATH="$_EXPRS_DEPS:$_EXPRS_BUILD:$PATH"
export QT_QPA_PLATFORM=offscreen

# Keep compile/test concurrency at the mandated ceiling.
export CMAKE_BUILD_PARALLEL_LEVEL=1
export CTEST_PARALLEL_LEVEL=1

# GDAL/PROJ data must resolve for hermetic runs. Without PROJ_DATA the
# GDAL/PROJ build shipped by vcpkg cannot find proj.db and every CRS-touching
# operator (io:clip, io:warp, ...) throws
#   "InvalidParameter: input raster carries no CRS"
#   "InvalidParameter: warpRaster: option construction failed"
# which masquerades as a product defect. FOUND during R0.5 (see EVIDENCE.md
# E-2b): 5 of the 6 red 11.0 suites were this single missing data path.
_EXPRS_VCPKG="$(pwd)/build-v12/vcpkg_installed/x64-windows"
if [ ! -d "$_EXPRS_VCPKG/share/proj" ]; then
  _EXPRS_VCPKG="/c/deps/vcpkg/installed/x64-windows"
fi
if [ -d "$_EXPRS_VCPKG/share/proj" ]; then
  export PROJ_DATA="$_EXPRS_VCPKG/share/proj"
  export PROJ_LIB="$PROJ_DATA"
fi
if [ -d "$_EXPRS_VCPKG/share/gdal" ]; then
  export GDAL_DATA="$_EXPRS_VCPKG/share/gdal"
fi
