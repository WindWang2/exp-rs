#!/usr/bin/env bash
# WP01 — D-T05-CHAIN1 six-hypothesis baseline (R29-T02).
# Exclusive scratch: /tmp/r29t02-wp01-* (NEVER touches shared /tmp/sicnu_operators).
# Pre-fix expectations: H-C (and H-D) reproduce the false-green; post-fix all pass.
set -uo pipefail
WT=/home/kevin/projects/rs-studio/exp-rs-r29-t02-data-runtime-20261108T164136Z
BUILD="$WT/build-dev"
CLI="$BUILD/sicnu_geo_rs_cli"
STAGE=$(mktemp -d /tmp/r29t02-wp01-XXXXXX)
PASS=0; FAIL=0
note() { echo "[wp01] $*"; }
check() { if [ "$1" = 0 ]; then PASS=$((PASS+1)); note "PASS: $2"; else FAIL=$((FAIL+1)); note "FAIL($1): $2"; fi; }

export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH=/home/kevin/.cache/exp-rs-agent/opencv5-prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

note "stage=$STAGE"

# Fresh deterministic sample input (offline, seed contract).
"$BUILD/tools/sicnu_generate_samples" --out="$STAGE/data" >"$STAGE/gen.log" 2>&1
check $? "sample generation"

INPUT="$STAGE/data/landsat_sample.tif"
OUT="$STAGE/out/ndvi.tif"
STATE="$STAGE/state/chunked-r29t02"   # exclusive resume-state base (marker: $STATE.published)
mkdir -p "$STAGE/out" "$STAGE/state"

make_pipeline() { # $1 = output path, $2 = state base
  python3 - "$INPUT" "$1" "$2" <<'PY'
import json, sys
inp, out, state = sys.argv[1], sys.argv[2], sys.argv[3]
d = {
  "name": "r29t02-wp01", "version": "1",
  "steps": [{
    "operator": "rs:spectral_index",
    "params": {"input": inp, "output": out, "index": "NDVI", "nir": 5, "red": 4,
               "resumeStateBase": state}
  }]
}
json.dump(d, open(state + ".pipeline.json", "w"), indent=2)
PY
}

run_pipe() { # $1 = pipeline json
  ( cd "$STAGE" && SICNU_OFFLINE=1 "$CLI" --offline --pipeline "$1" ) >"$STAGE/last_run.log" 2>&1
  return $?
}

# ---------- H-A: first run writes the artifact ----------
make_pipeline "$OUT" "$STATE"
run_pipe "$STATE.pipeline.json"
check $? "H-A first run exits 0"
[ -s "$OUT" ]; check $? "H-A artifact produced"
MARKER="$STATE.published"
[ -f "$MARKER" ]; check $? "H-A published marker written"

# ---------- H-B: identical rerun is a legal cache hit ----------
BEFORE=$(stat -c %Y "$OUT")
sleep 1.1
run_pipe "$STATE.pipeline.json"
check $? "H-B rerun exits 0"
[ -s "$OUT" ]; check $? "H-B artifact still present"
AFTER=$(stat -c %Y "$OUT")
[ "$BEFORE" = "$AFTER" ]; check $? "H-B artifact untouched (honest cache hit, mtime $BEFORE=$AFTER)"

# ---------- H-C: artifact deleted, marker kept (THE false-green) ----------
rm -f "$OUT"
[ -f "$MARKER" ]; check $? "H-C marker still present after artifact deletion"
run_pipe "$STATE.pipeline.json"
check $? "H-C rerun exits 0"
[ -s "$OUT" ]; check $? "H-C artifact REBUILT (deleted output self-heals, no false green)"
C_MTIME=$(stat -c %Y "$OUT")
if grep -q "output not openable" "$STAGE/last_run.log"; then
  FAIL=$((FAIL+1)); note "FAIL: H-C CLI still warns 'output not openable' (false-green symptom present)"
else
  PASS=$((PASS+1)); note "PASS: H-C no 'output not openable' warning"
fi

# ---------- H-D: input CONTENT replaced at the same path ----------
sleep 1.1
python3 - "$INPUT" <<'PY'
# Rewrite the same path with different (shifted) bytes — content drift.
import os, sys
from osgeo import gdal
path = sys.argv[1]
src = gdal.Open(path, gdal.GA_ReadOnly)
drv = gdal.GetDriverByName('GTiff')
w, h = src.RasterXSize, src.RasterYSize
out = drv.Create(path + '.new', w, h, src.RasterCount, gdal.GDT_Float32)
out.SetGeoTransform(src.GetGeoTransform())
out.SetProjection(src.GetProjection())
for b in range(1, src.RasterCount + 1):
    band = src.GetRasterBand(b).ReadAsArray()
    band = band * 0.5 + 10.0  # different content
    out.GetRasterBand(b).WriteArray(band)
out = None
src = None
os.replace(path + '.new', path)
PY
check $? "H-D input rewritten in place"
sleep 1.1
run_pipe "$STATE.pipeline.json"
check $? "H-D rerun exits 0"
NEW_MTIME=$(stat -c %Y "$OUT")
[ -n "$NEW_MTIME" ] && [ "$NEW_MTIME" != "$C_MTIME" ]; check $? "H-D output regenerated after input content drift (mtime $C_MTIME -> $NEW_MTIME)"

# ---------- H-E: cross-workdir — same operator/input/output, fresh state base ----------
STATE2="$STAGE/state2/chunked-r29t02"; mkdir -p "$STAGE/state2"
make_pipeline "$OUT" "$STATE2"
run_pipe "$STATE2.pipeline.json"
check $? "H-E cross-state run exits 0"
[ -s "$OUT" ]; check $? "H-E artifact present after fresh-state run (no cross-workspace false green)"

echo "[wp01] RESULT PASS=$PASS FAIL=$FAIL  stage=$STAGE"
[ "$FAIL" = 0 ]
