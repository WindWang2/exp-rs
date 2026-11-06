#!/usr/bin/env bash
# WP12 chain ①: offline sample → process → required-stage validation → grading with explainable feedback.
# Includes one error/cancel scenario and one recovery/rerun scenario.
# Run AFTER the full build completes. All paths under this worktree.
set -uo pipefail
WT=/home/kevin/projects/rs-studio/exp-rs-r28-t05-workflow-teaching-delivery-20261104T210720Z
BUILD="$WT/build-r28-t05"
CLI="$BUILD/sicnu_geo_rs_cli"
STAGE=/tmp/r28t05-chain1
PASS=0; FAIL=0
note() { echo "[chain1] $*"; }
check() { if [ "$1" = 0 ]; then PASS=$((PASS+1)); note "PASS: $2"; else FAIL=$((FAIL+1)); note "FAIL($1): $2"; fi; }

rm -rf "$STAGE"; mkdir -p "$STAGE" "$STAGE/data" "$STAGE/output"
# D-T05-CHAIN1 (T02 lane, root-caused by R2): clear the /tmp chunked-run
# resume marker so the pipeline exercises the real write path; the marker is
# keyed on operator+input identity only, so a prior run on this host would
# otherwise make every later run report success with no artifact.
rm -rf /tmp/sicnu_operators/chunked-*
export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH=/home/kevin/.cache/exp-rs-agent/opencv5-prefix/lib

# 1. deterministic sample generation (offline, seed contract)
"$BUILD/tools/sicnu_generate_samples" --out="$STAGE/data/samples" >"$STAGE/gen.log" 2>&1
check $? "sample generation"

# 2. absolute-path pipeline copy (TaskCenter worker cwd is not the shell cwd)
python3 -c "
import json
src = '/home/kevin/projects/rs-studio/exp-rs-r28-t05-workflow-teaching-delivery-20261104T210720Z/packaging/bundle/labs/lab1/lab1_ndvi.pipeline.json'
d = json.load(open(src))
d['steps'][0]['params']['input'] = '$STAGE/data/samples/landsat_sample.tif'
d['steps'][0]['params']['output'] = '$STAGE/lab1_ndvi.tif'
json.dump(d, open('$STAGE/lab1_abs.pipeline.json','w'), indent=2)
"

# 2b. offline pipeline run (cwd=STAGE so the output lands there; absolute sample input)
( cd "$STAGE" && SICNU_OFFLINE=1 "$CLI" --offline --pipeline "$STAGE/lab1_abs.pipeline.json" \
    --experiment-record "$STAGE/teach.db" --experiment-id chain1-lab1 --experiment-name "R28-T05 chain1" ) \
  >"$STAGE/run1.log" 2>&1
check $? "lab1 pipeline run (offline, recorded)"

# 3. wait for the pipeline artifact (bounded poll).
# KNOWN DEFECT (D-T05-CHAIN1, T02 lane): the CLI tracked-pipeline path can
# report success without ever sinking tiles (no output file). The chain
# records that honestly: when the artifact is absent the pipeline's
# success claim is FALSE-GREEN evidence for the issue, not a chain pass.
for _ in $(seq 1 60); do [ -s "$STAGE/lab1_ndvi.tif" ] && break; sleep 1; done
if [ -s "$STAGE/lab1_ndvi.tif" ]; then
  PASS=$((PASS+1)); note "PASS: pipeline artifact produced"
else
  FAIL=$((FAIL+1)); note "FAIL: pipeline reported success but produced NO artifact (D-T05-CHAIN1 false-green)"
fi

# 3b. grading with explainable feedback against the calibrated overlay rules
SICNU_OFFLINE=1 "$CLI" lab --lab ndvi_basics --grade "$STAGE/lab1_ndvi.tif" \
  --out "$STAGE/grade1.json" >"$STAGE/grade1.log" 2>&1
# verdict=fail exits nonzero by contract; the CHECK is that a VALID graded report exists
[ -s "$STAGE/grade1.json" ]; check $? "grading run (report produced)"
[ -s "$STAGE/grade1.json" ] && python3 -c "
import json,sys
d=json.load(open('$STAGE/grade1.json'))
r=d.get('report',{})
print('[chain1] verdict:',r.get('verdict'),'score:',r.get('score'),'/',r.get('totalPoints'))
required=r.get('requiredStageOutcomes') or []
missing=[s for s in required if not s.get('satisfied')]
print('[chain1] required stages:',len(required),'unsatisfied:',len(missing))
# A parseable report is not enough: requiredStageOutcomes must exist and all
# of those stages must actually be satisfied.
sys.exit(0 if required and not missing else 1)"
check $? "grade report parses + required stages present"

# 4. ERROR scenario: grading a MISSING artifact must fail with a typed error, not fabricate a pass
SICNU_OFFLINE=1 "$CLI" lab --lab ndvi_basics --grade "$STAGE/data/samples/does_not_exist.tif" \
  >"$STAGE/grade_missing.log" 2>&1
rc=$?; [ "$rc" -ne 0 ] && grep -qiE "error|missing|not found" "$STAGE/grade_missing.log"
check $? "missing-artifact grading fails with typed error (rc=$rc)"

# 5. CANCEL scenario: pipeline timeout path (SICNU_PIPELINE_TIMEOUT_SEC parse — #1812 fix) — a garbage value must NOT become unlimited
SICNU_PIPELINE_TIMEOUT_SEC=garbage SICNU_OFFLINE=1 timeout 60 "$CLI" --offline --pipeline "$STAGE/lab1_abs.pipeline.json" >"$STAGE/run_timeout.log" 2>&1
rc=$?
note "timeout-parse scenario rc=$rc (non-crash, bounded run)"
[ "$rc" -ne 124 ] # timeout(1) killing it would mean unlimited-wall regression
check $? "garbage SICNU_PIPELINE_TIMEOUT_SEC does not become unlimited (rc=$rc)"

# 6. RECOVERY scenario: rerun the same pipeline after the error/cancel runs — idempotent success
( cd "$STAGE" && SICNU_OFFLINE=1 "$CLI" --offline --pipeline "$STAGE/lab1_abs.pipeline.json" \
    --experiment-record "$STAGE/teach.db" --experiment-id chain1-lab1 ) >"$STAGE/run2.log" 2>&1
check $? "rerun after error scenarios succeeds"

echo "[chain1] RESULT PASS=$PASS FAIL=$FAIL"
[ "$FAIL" = 0 ]
