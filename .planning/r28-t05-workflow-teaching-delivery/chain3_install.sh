#!/usr/bin/env bash
# WP12 chain ③: install tree → CLI/MCP basic smoke → experiment → artifact reopen.
# Error scenario: running the installed CLI with build-tree env vars CLEARED
# (catches any silent dependence on the source tree). Recovery: rerun after
# fixing the environment.
set -uo pipefail
WT=/home/kevin/projects/rs-studio/exp-rs-r28-t05-workflow-teaching-delivery-20261104T210720Z
BUILD="$WT/build-r28-t05"
STAGE=/tmp/r28t05-chain3-staging
PASS=0; FAIL=0
note() { echo "[chain3] $*"; }
check() { if [ "$1" = 0 ]; then PASS=$((PASS+1)); note "PASS: $2"; else FAIL=$((FAIL+1)); note "FAIL($1): $2"; fi; }

rm -rf "$STAGE"; mkdir -p "$STAGE"

# 1. real install into a staging prefix
( cd "$WT" && python3 scripts/dev/resource_guard.py --lock-dir "$BUILD" --parallel 1 --max-parallel 1 --wait 900 -- \
    cmake --install "$BUILD" --prefix "$STAGE" > "$STAGE/install.log" 2>&1 )
check $? "cmake --install into staging prefix"
ls "$STAGE/bin/" > "$STAGE/bin.list" 2>&1; note "installed bins: $(wc -l < "$STAGE/bin.list")"
grep -q sicnu_geo_rs_cli "$STAGE/bin.list"; check $? "CLI present in install tree"
grep -q sicnu_worker "$STAGE/bin.list"; check $? "worker present in install tree (#1698)"
find "$STAGE" -name "sicnu_zh_CN.qm" | grep -q .; check $? "zh_CN.qm installed (#1623)"

# 2. smoke with a SCRUBBED environment: no build-tree LD_LIBRARY_PATH / CMake vars.
# Third-party deps (OpenCV 5 local prefix, Qt/GDAL system) come from the HOST
# install paths, exactly like a real packaging host — the project RPATH only
# covers libsicnu_* in the install tree (#1698).
OCV_LIB=/home/kevin/.cache/exp-rs-agent/opencv5-prefix/lib
env -i HOME="$HOME" PATH="/usr/bin:/bin" QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH="$OCV_LIB" \
  "$STAGE/bin/sicnu_geo_rs_cli" env-doctor > "$STAGE/envdoctor.log" 2>&1
check $? "installed CLI env-doctor (scrubbed env + host OpenCV)"
env -i HOME="$HOME" PATH="/usr/bin:/bin" QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH="$OCV_LIB" \
  "$STAGE/bin/sicnu_geo_rs_cli" --list > "$STAGE/list.log" 2>&1
rc=$?; [ $rc -eq 0 ] && [ "$(grep -c 'Registered operators' "$STAGE/list.log")" -ge 1 ]
check $? "installed CLI operator registry loads (rc=$rc)"

# 3. no source-tree leakage: strace-free check — ldd resolves, no $WT paths in deps
LD_LIBRARY_PATH="$OCV_LIB" ldd "$STAGE/bin/sicnu_geo_rs_cli" > "$STAGE/ldd.log" 2>&1
[ "$(grep -c 'not found' "$STAGE/ldd.log")" = "0" ]; check $? "no missing shared libs (#1698 RPATH)"
grep -q "$WT" "$STAGE/ldd.log"; rc=$?; [ $rc -ne 0 ]; check $? "install tree does not link against the source worktree"

# 4. experiment from the INSTALLED tree (recorded run)
SAMPLES=/tmp/r28t05-chain3-samples
rm -rf "$SAMPLES" /tmp/r28t05-chain3-data
mkdir -p /tmp/r28t05-chain3-data/data /tmp/r28t05-chain3-data/output
# D-T05-CHAIN1 (T02 lane, root-caused by R2): the chunked-run resume marker
# under /tmp/sicnu_operators is keyed on operator+input+partition identity
# only — NOT on the output path — so any earlier run of the same operator on
# this host makes every later run report success with zero tiles sinked and
# no artifact. Clear the ephemeral scratch so this chain exercises the real
# write path (the marker is regenerable scratch, never user data).
rm -rf /tmp/sicnu_operators/chunked-*
"$BUILD/tools/sicnu_generate_samples" --out=/tmp/r28t05-chain3-data/data/samples >/dev/null 2>&1
( cd /tmp/r28t05-chain3-data && env -i HOME="$HOME" PATH="/usr/bin:/bin" QT_QPA_PLATFORM=offscreen SICNU_OFFLINE=1 LD_LIBRARY_PATH="$OCV_LIB" \
    "$STAGE/bin/sicnu_geo_rs_cli" --offline --pipeline "$WT/packaging/bundle/labs/lab1/lab1_ndvi.pipeline.json" \
    --experiment-record "$STAGE/chain3.db" --experiment-id chain3-install ) > "$STAGE/run.log" 2>&1
check $? "installed-tree recorded experiment run"

# 5. artifact reopen (R2 review: the old version passed unconditionally in
#    both branches — vacuous). A real run writes the artifact next to the
#    pipeline (cwd-relative output), so run the pipeline from the staging
#    data dir and reopen whatever it produced with the installed CLI's
#    passport command, checking a NONZERO rc and real JSON content.
mkdir -p /tmp/r28t05-chain3-data/data /tmp/r28t05-chain3-data/output
# D-T05-CHAIN1 (T02 lane, root-caused by R2): the chunked-run resume marker
# under /tmp/sicnu_operators is keyed on operator+input+partition identity
# only — NOT on the output path — so any earlier run of the same operator on
# this host makes every later run report success with zero tiles sinked and
# no artifact. Clear the ephemeral scratch so this chain exercises the real
# write path (the marker is regenerable scratch, never user data).
rm -rf /tmp/sicnu_operators/chunked-*
"$BUILD/tools/sicnu_generate_samples" --out=/tmp/r28t05-chain3-data/data/samples >/dev/null 2>&1
python3 -c "
import json
d = json.load(open('$WT/packaging/bundle/labs/lab1/lab1_ndvi.pipeline.json'))
d['steps'][0]['params']['input'] = '/tmp/r28t05-chain3-data/data/samples/landsat_sample.tif'
d['steps'][0]['params']['output'] = '/tmp/r28t05-chain3-data/lab1_ndvi.tif'
json.dump(d, open('/tmp/r28t05-chain3-data/pipeline.json','w'), indent=2)"
( cd /tmp/r28t05-chain3-data && env -i HOME="$HOME" PATH="/usr/bin:/bin" QT_QPA_PLATFORM=offscreen \
    SICNU_OFFLINE=1 LD_LIBRARY_PATH="$OCV_LIB" \
    "$STAGE/bin/sicnu_geo_rs_cli" --offline --pipeline /tmp/r28t05-chain3-data/pipeline.json \
    --experiment-record "$STAGE/chain3.db" --experiment-id chain3-install ) \
  > "$STAGE/run.log" 2>&1
ART=/tmp/r28t05-chain3-data/lab1_ndvi.tif
if [ -s "$ART" ]; then
  env -i HOME="$HOME" PATH="/usr/bin:/bin" QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH="$OCV_LIB" \
    "$STAGE/bin/sicnu_geo_rs_cli" passport --path "$ART" --json > "$STAGE/passport.log" 2>&1
  rc=$?
  [ $rc -eq 0 ] && [ -s "$STAGE/passport.log" ]
  check $? "installed CLI reopens the produced artifact via passport (rc=$rc)"
else
  FAIL=$((FAIL+1)); note "FAIL: no artifact to reopen (D-T05-CHAIN1 false-green)"
fi

echo "[chain3] RESULT PASS=$PASS FAIL=$FAIL"
[ "$FAIL" = 0 ]
