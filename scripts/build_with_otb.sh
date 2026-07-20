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

# boost-libs without dev headers needs ABI-matched includes (see cmake/sicnu_boost.cmake)
if ! compgen -G "$SRC_DIR/vendor/boost_sys/usr/include/boost/version.hpp" > /dev/null \
   && [[ ! -f /usr/include/boost/version.hpp ]]; then
    echo "Fetching Boost headers to match boost-libs..."
    "$SRC_DIR/scripts/fetch_boost_headers.sh"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$SRC_DIR" \
    -DENABLE_TESTS=ON \
    -DSICNU_BUILD_OTB=ON \
    -DCMAKE_BUILD_TYPE=Release

echo ""
echo "=== Building (this may take 30-45 minutes on first run) ==="
# ITK is static PIC; OTB is shared (BUILD_SHARED_LIBS=ON after ITK) so composite
# apps like TrainImagesClassifier share one ApplicationEngine.
make -j"$(nproc)"

# Stage CLI launchers + shared OTB libs into tools/otb for ToolPathManager
if [[ -x "${SRC_DIR}/scripts/bundle_otb_tools.sh" ]]; then
    echo ""
    echo "=== Bundling OTB tools → ${BUILD_DIR}/tools/otb ==="
    "${SRC_DIR}/scripts/bundle_otb_tools.sh" "$(pwd)" "${SRC_DIR}"
fi

echo ""
echo "=== Running tests ==="
export SICNU_OTB_PATH="$(pwd)/tools/otb"
export LD_LIBRARY_PATH="$(pwd)/lib:${LD_LIBRARY_PATH:-}"
export PATH="$(pwd)/bin:${PATH}"
# Prefer OTB unit tests when present; full ctest is large.
if [[ -x tests/test_otb_operators ]]; then
    ./tests/test_otb_operators || true
fi
ctest --output-on-failure -R 'otb|rs_operator' || ctest --output-on-failure

echo ""
echo "=== Build complete ==="
echo "Use OTB CLI / RSOperators from the default (non-OTB) build tree with:"
echo "  export SICNU_OTB_PATH=\"$(pwd)/tools/otb\""
echo "  export LD_LIBRARY_PATH=\"$(pwd)/lib:\${LD_LIBRARY_PATH}\""
echo "  # or: source $(pwd)/tools/otb/otbenv.profile"
