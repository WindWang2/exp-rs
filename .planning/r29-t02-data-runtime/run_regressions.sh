#!/usr/bin/env bash
# R29-T02 regression runner — build-dev, bounded by the host lock + ctest -j1.
# Usage: bash .planning/r29-t02-data-runtime/run_regressions.sh [regex-filter]
set -uo pipefail
WT=/home/kevin/projects/rs-studio/exp-rs-r29-t02-data-runtime-20261108T164136Z
BUILD="$WT/build-dev"
OUT="$WT/.planning/r29-t02-data-runtime"
FILTER="${1:-r29_t02|chunk_resume_11|chunk_adoption_11|chunk_contract_11|runtime_chunk_boundaries_r4|r27_t02_resume_lock|chunked_operators_parity|chunked_fabric_e2e|output_committer|io_atomic_failures|workflow_cache_e2e|r28_t02_|virtual_raster_preflight|virtual_raster_create|swarm_fix_data|task_center|job_engine|execution_plane}"

export QT_QPA_PLATFORM=offscreen
export CTEST_PARALLEL_LEVEL=1 OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1
export LD_LIBRARY_PATH=/home/kevin/.cache/exp-rs-agent/opencv5-prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

cd "$WT"
ctest --test-dir "$BUILD" --show-only=json-v1 -R "$FILTER" > "$OUT/ctest-match.json" 2>/dev/null
MATCHES=$(python3 -c "
import json,sys
try:
    d=json.load(open('$OUT/ctest-match.json'))
    print(len(d.get('tests',[])))
except Exception as e:
    print(0)
")
echo "[regress] filter='$FILTER' matched=$MATCHES tests"
if [ "$MATCHES" = 0 ]; then echo "[regress] FAIL: 0 tests matched (filter error?)"; exit 2; fi

python3 scripts/dev/resource_guard.py \
  --lock-dir "${XDG_CACHE_HOME:-$HOME/.cache}/exp-rs-agent/host-build" --parallel 1 --max-parallel 4 --wait 3500 -- \
  ctest --test-dir "$BUILD" -j1 --output-on-failure -R "$FILTER" --timeout 900 \
  > "$OUT/ctest-run.log" 2>&1
RC=$?
echo "[regress] ctest exit=$RC (log: $OUT/ctest-run.log)"
grep -E 'tests passed|tests failed' "$OUT/ctest-run.log" | tail -3
exit $RC
