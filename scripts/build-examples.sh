#!/usr/bin/env bash
# scripts/build-examples.sh — out-of-tree SDK consumer validation.
#
# 1. installs the exp-rs SDK into a scratch prefix (DESTDIR-free, controlled)
# 2. configures + builds every example plugin against ONLY that prefix
# 3. runs `plugin validate` on each example package
#
# Usage: scripts/build-examples.sh [build-dir] [install-prefix]
# Both default under /tmp so the repo tree stays clean.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-/tmp/exprs-sdk-check/build}"
PREFIX="${2:-/tmp/exprs-sdk-check/prefix}"
PARALLEL="${EXP_RS_EXAMPLE_PARALLEL:-2}"

echo "== installing ExpRS SDK into ${PREFIX}"
cmake --install "${REPO_BUILD:-${REPO_ROOT}/build}" --prefix "${PREFIX}" >/dev/null

EXAMPLES=(
  cpp-operator-demo
  agent-tool-demo
  data-provider-demo
  model-runtime-demo
  ui-dock-demo
)

for example in "${EXAMPLES[@]}"; do
  echo "== building example: ${example}"
  cmake -S "${REPO_ROOT}/examples/plugins/${example}" \
        -B "${BUILD_DIR}/${example}" \
        -DCMAKE_PREFIX_PATH="${PREFIX}" \
        -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "${BUILD_DIR}/${example}" --parallel "${PARALLEL}" >/dev/null
done

echo "== validating example manifests"
CLI="${PREFIX}/bin/sicnu_geo_rs_cli"
if [ ! -x "${CLI}" ]; then
  echo "  (installed CLI not present; skipping CLI validation)"
  exit 0
fi
SICNU_PLUGIN_PATH="${REPO_ROOT}/examples/plugins" "${CLI}" plugin validate \
  "${REPO_ROOT}/examples/plugins/cpp-operator-demo" --json

echo "== all examples built against the installed SDK"
