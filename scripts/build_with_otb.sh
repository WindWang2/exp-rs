#!/usr/bin/env bash
# Build SICNU GEO RS with OTB + ITK vendored libraries enabled.
# Usage: ./scripts/build_with_otb.sh [build_dir]
# Default build_dir: build-otb
set -euo pipefail

BUILD_DIR="${1:-build-otb}"
SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "=== Building with OTB enabled ==="
echo "Source: $SRC_DIR"
echo "Build:  $BUILD_DIR"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$SRC_DIR" \
    -DENABLE_TESTS=ON \
    -DSICNU_BUILD_OTB=ON \
    -DCMAKE_BUILD_TYPE=Release

echo ""
echo "=== Building (this may take 30-45 minutes on first run) ==="
make -j"$(nproc)"

echo ""
echo "=== Running tests ==="
ctest --output-on-failure

echo ""
echo "=== Build complete ==="
