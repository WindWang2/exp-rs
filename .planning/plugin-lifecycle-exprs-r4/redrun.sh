#!/bin/bash
# WP-A red/green runner for the P1-9 three failing TEST_CASEs.
# Usage: redrun.sh <build-dir> <pass-label>
# Runs test_exprs_plugin_loader with the three P1-9 case selectors and
# captures each case's verdict into a per-pass log under /tmp.
set -u
BUILD="${1:?build dir}"
LABEL="${2:?pass label}"
BIN="$BUILD/bin/test_exprs_plugin_loader"
LOG="/tmp/p19_${LABEL}.log"
if [ ! -x "$BIN" ]; then BIN="$BUILD/test_exprs_plugin_loader"; fi
echo "== P1-9 red/green pass: $LABEL ==" | tee "$LOG"
export QT_QPA_PLATFORM=offscreen
# LD path for GDAL/PROJ from the local SDK prefix (repo convention on this host).
export LD_LIBRARY_PATH="/home/kevin/pwb-sdks/root/usr/lib:${LD_LIBRARY_PATH:-}"
for SEL in \
  '"hot reload swaps in a valid new manifest and refuses a broken one"' \
  '"installOrUpgrade installs a fresh package, then atomically upgrades it"' \
  '"installOrUpgrade rolls back when the new version cannot load"'; do
  echo "--- CASE $SEL" | tee -a "$LOG"
  "$BIN" "$SEL" --order decl >> "$LOG" 2>&1
  echo "EXIT=$?" | tee -a "$LOG"
done
echo "== summary ==" | tee -a "$LOG"
grep -E 'FAILED|^EXIT|All tests passed|assertions' "$LOG" | tail -20
