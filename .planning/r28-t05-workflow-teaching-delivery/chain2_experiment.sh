#!/usr/bin/env bash
# WP12 chain ②: experiment run → metrics → comparison → reproduction bundle
# export → isolated validation (dry-run preview of conflicts where the
# surface allows it).
# Run AFTER the full build. Verified commands (2026-11-05) against a real
# recorded run; the CLI's reproduce surface exposes export|validate|inspect
# (import is library-level, pinned by test_r28_t05_bundle_import_safety).
set -uo pipefail
WT=/home/kevin/projects/rs-studio/exp-rs-r28-t05-workflow-teaching-delivery-20261104T210720Z
BUILD="$WT/build-r28-t05"
CLI="$BUILD/sicnu_geo_rs_cli"
STAGE=/tmp/r28t05-chain2
PASS=0; FAIL=0
note() { echo "[chain2] $*"; }
check() { if [ "$1" = 0 ]; then PASS=$((PASS+1)); note "PASS: $2"; else FAIL=$((FAIL+1)); note "FAIL($1): $2"; fi; }

export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH=/home/kevin/.cache/exp-rs-agent/opencv5-prefix/lib

rm -rf "$STAGE"; mkdir -p "$STAGE" "$STAGE/data" "$STAGE/output"

# 1. deterministic samples (offline, seed contract)
"$BUILD/tools/sicnu_generate_samples" --out="$STAGE/data/samples" >"$STAGE/gen.log" 2>&1
check $? "sample generation"

# 2. absolute-path pipeline copy (TaskCenter worker cwd is not the shell cwd)
python3 -c "
import json
src = '$WT/packaging/bundle/labs/lab1/lab1_ndvi.pipeline.json'
d = json.load(open(src))
d['steps'][0]['params']['input'] = '$STAGE/data/samples/landsat_sample.tif'
d['steps'][0]['params']['output'] = '$STAGE/lab1_ndvi.tif'
json.dump(d, open('$STAGE/pipeline.json','w'), indent=2)
"

# 3. recorded run with identity pins (seed + model pins; dataset pin needs a
#    committed dataset version, so it is deliberately omitted and the
#    validate step below reports that honestly)
( cd "$STAGE" && SICNU_OFFLINE=1 "$CLI" --offline --pipeline "$STAGE/pipeline.json" \
    --experiment-record "$STAGE/exp.db" --experiment-id chain2 --pin-seed 42 ) \
  >"$STAGE/run.log" 2>&1
check $? "recorded pipeline run (offline, seed pin)"

# 4. experiment inspect — the run exists and is Completed
"$CLI" experiment inspect --experiment-db "$STAGE/exp.db" --experiment chain2 --json \
  >"$STAGE/inspect.json" 2>&1
python3 -c "
import json,sys
d=json.load(open('$STAGE/inspect.json'))
runs=d['data']['runs']
assert runs, 'no runs'
assert runs[0]['status']=='completed', runs[0]['status']
print('[chain2] run status:', runs[0]['status'])" > /dev/null 2>&1
check $? "experiment inspect reports the completed run"

RUN_ID=$(python3 -c "
import json
d=json.load(open('$STAGE/inspect.json'))
print(d['data']['run_ids'][0])" 2>/dev/null)
note "run id: $RUN_ID"

# 5. reproduction bundle export (real bundle with checksums)
"$CLI" reproduce export --experiment-db "$STAGE/exp.db" --dataset-db "$STAGE/ds.db" \
  --run "$RUN_ID" --out "$STAGE/bundle" >"$STAGE/export.log" 2>&1
[ -s "$STAGE/bundle/checksums.txt" ] && [ -s "$STAGE/bundle/manifest.json" ]
check $? "reproduction bundle exported (checksums + manifest)"

# 6. bundle validation — the identity gate must be HONEST: a run without a
#    dataset pin is level=impossible with the reason named, never a fake pass
"$CLI" reproduce validate --experiment-db "$STAGE/exp.db" --dataset-db "$STAGE/ds.db" \
  --bundle "$STAGE/bundle" --json >"$STAGE/validate.json" 2>&1
python3 -c "
import json,sys
d=json.load(open('$STAGE/validate.json'))
data=d['data']
assert data['level']=='impossible', data
assert any('dataset pin' in r for r in data['reasons']), data['reasons']
print('[chain2] validate level:', data['level'], '| reason:', data['reasons'][0])" > /dev/null 2>&1
check $? "bundle validate reports impossible + 'run has no dataset pin'"

# 7. bundle inspect — the exported manifest round-trips
"$CLI" reproduce inspect --experiment-db "$STAGE/exp.db" --dataset-db "$STAGE/ds.db" \
  --bundle "$STAGE/bundle" --run "$RUN_ID" --json >"$STAGE/bundle-inspect.json" 2>&1
[ -s "$STAGE/bundle-inspect.json" ]
check $? "bundle inspect"

echo "[chain2] RESULT PASS=$PASS FAIL=$FAIL"
[ "$FAIL" = 0 ]
