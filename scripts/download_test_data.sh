#!/usr/bin/env bash
# SICNU GEO RS — Task 11.5.6 test-data download stub.
#
# The plan (docs/superpowers/plans/2026-06-03-georeferencer-v15-implementation.md
# §Task 6) originally promised a script that pulls a Landsat 9 L1TP scene and
# an SRTM DEM tile via git-LFS for the RPC golden regression.  In the
# synthetic path adopted by Task 11.5.6 there is NO external data: the
# golden raster, the DEM, and the warp output are all generated in-memory
# by the test helpers in `tests/warper_test_helpers.h`, and the only
# committed artifact is a SHA256 fingerprint in
# `tests/data/georef/golden/synthetic_rpc_warp.sha256`.
#
# This stub exists so the workflow promised by the spec is still visible
# (and so future work that swaps in real LC09 data has a place to hook into).

set -euo pipefail

echo "[download_test_data] No external test data required for the RPC golden"
echo "[download_test_data] regression.  Task 11.5.6 uses the synthetic fixture"
echo "[download_test_data] from tests/warper_test_helpers.h."
exit 0
