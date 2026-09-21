#!/usr/bin/env bash
# Run the exp-rs verification test executables with a fully-formed runtime
# environment.  Usage:
#   tools/verification12/run_tests.sh <build-dir> <test-exe-name> [...]
#
# Why a script: three separate environment facts are needed simultaneously and
# getting any one wrong produces a *misleading* failure rather than a clear
# error:
#   1. PATH must include Qt / vcpkg-debug / qca / kc / build-dir DLLs, else
#      the exe dies with exit 127 "error while loading shared libraries".
#   2. PROJ_DATA / GDAL_DATA must be a *Windows-style* absolute path, else
#      GDAL/PROJ report "Cannot find proj.db" and every CRS operator throws
#      "InvalidParameter: input raster carries no CRS" — which looks exactly
#      like a product defect.  A Unix-style (/c/deps/...) value also fails;
#      the value is consumed by the native PROJ 9 library.
#   3. QT_QPA_PLATFORM=offscreen so no window server is required.
#
# Verified in R0.5/R1.1: with this recipe test_verification_numeric_reference_11
# goes from 1 failed assertion to "All tests passed (234 assertions)".

set -u

export PATH="/c/Program Files/Git/usr/bin:/c/Program Files/Git/cmd:$PATH"

BUILD_DIR="${1:-C:/Users/wangj.KEVIN/projects/exp-rs-worktrees/glm53-scientific-verification-12/build-v12}"
shift || true

export PATH="/c/deps/Qt/6.8.0/msvc2022_64/bin:/c/deps/vcpkg/installed/x64-windows/debug/bin:/c/deps/vcpkg/installed/x64-windows/bin:/c/deps/qca-install/bin:/c/deps/kc-install/bin:$BUILD_DIR:$PATH"
export QT_QPA_PLATFORM=offscreen

# GDAL/PROJ data — a Windows-style absolute path is mandatory.
PROJ_SHARE=""
for _base in "/c/deps/vcpkg/installed/x64-windows" "$BUILD_DIR/vcpkg_installed/x64-windows"; do
  if [ -f "$_base/share/proj/proj.db" ]; then
    case "$_base" in
      /c/*) PROJ_SHARE="C:${_base#/c}" ;;
      *)    PROJ_SHARE="$_base" ;;
    esac
    PROJ_SHARE="${PROJ_SHARE//\//\\}"
    break
  fi
done

if [ -n "$PROJ_SHARE" ]; then
  export PROJ_DATA="$PROJ_SHARE\\share\\proj"
  export PROJ_LIB="$PROJ_DATA"
  export GDAL_DATA="$PROJ_SHARE\\share\\gdal"
fi

if [ $# -eq 0 ]; then
  echo "usage: $0 <build-dir> <test-exe> [test-exe ...]" >&2
  exit 2
fi

# classroom-safety 13.0: the loop used to print each exit code and then let the
# script fall off the end, so the script itself always exited 0 — a red suite
# looked green to any caller. Aggregate and propagate.
FAILED=0
for t in "$@"; do
  echo "########## $t ##########"
  "$BUILD_DIR/$t" 2>&1
  RC=$?
  echo "exit=$RC"
  if [ "$RC" -ne 0 ]; then
    FAILED=1
  fi
done

if [ "$FAILED" -ne 0 ]; then
  echo "run_tests: FAIL (at least one lane did not exit 0)"
  exit 1
fi
echo "run_tests: all lanes passed"
