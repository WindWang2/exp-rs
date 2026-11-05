#!/usr/bin/env bash
# R28-T05 post-build verification pipeline. Runs after the full tree build.
# Stages: inventory → track regressions → snapshot regen + drift → install →
# chains → fast-lane classification. Every stage logs; failures don't abort
# later independent stages (except build-dependency chains).
set -uo pipefail
WT=/home/kevin/projects/rs-studio/exp-rs-r28-t05-workflow-teaching-delivery-20261104T210720Z
BUILD="$WT/build-r28-t05"
LOG=/tmp/r28t05-pipeline
HOST_LOCK="$HOME/.cache/exp-rs-agent/host-build"
GUARD="$WT/scripts/dev/resource_guard.py"
mkdir -p "$LOG"
export QT_QPA_PLATFORM=offscreen OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 CTEST_PARALLEL_LEVEL=1

stage() { echo "=== [$(date +%H:%M:%S)] $1 ==="; }

# A. inventory
stage "A inventory"
ctest --test-dir "$BUILD" -N > "$LOG/inventory.txt" 2>&1
ctest --test-dir "$BUILD" --show-only=json-v1 > "$LOG/inventory.json" 2>&1
python3 - "$LOG/inventory.json" <<'EOF' > "$LOG/inventory-summary.txt"
import json,sys
d=json.load(open(sys.argv[1]))
tests=d['tests']
built=[t for t in tests if 'NOT_BUILT' not in t['name']]
print(f"registered={len(tests)} built={len(built)} not_built={len(tests)-len(built)}")
EOF

# B. track regression suites (adjacent interfaces)
stage "B track regressions"
ctest --test-dir "$BUILD" -j1 --output-on-failure \
  -R "test_r28_t05|test_r27_t05|test_placeholder_grammar|test_experiment_evaluation|test_confusion_matrix|test_grader_engine|test_grader_schema|test_r18_e_grader_faultlab|test_scientific_planner_schema|test_scientific_planner_drift|test_mlops9_evidence|test_r25_b_replay_deviation_v2|test_experiment_consistency_r4|test_r15_B_geodata_persistence|test_workflow_execution_plane" \
  > "$LOG/regressions.log" 2>&1
echo "regressions_rc=$?" >> "$LOG/regressions.log"

# C. snapshot regeneration + drift suites (WP02) — under host lock (relayout)
stage "C snapshot regen"
python3 "$GUARD" --lock-dir "$HOST_LOCK" --parallel 1 --max-parallel 1 --wait 7200 -- \
  cmake --build "$BUILD" --parallel 1 --target surface_snapshot_regen > "$LOG/snapshot-build.log" 2>&1
echo "regen_rc=$?" >> "$LOG/snapshot.log"
git -C "$WT" status --short tests/surface_diff_snapshot.json >> "$LOG/snapshot.log" 2>&1
ctest --test-dir "$BUILD" -j1 --output-on-failure -R "test_surface_snapshot|test_snapshot_drift_12|test_snapshot_gate_r4|test_contract_cross_surface_11|test_contract_platform_9" > "$LOG/snapshot-tests.log" 2>&1
echo "snapshot_tests_rc=$?" >> "$LOG/snapshot-tests.log"

# D. install staging (WP10) + chain3
stage "D install + chain3"
bash "$WT/.planning/r28-t05-workflow-teaching-delivery/chain3_install.sh" > "$LOG/chain3.log" 2>&1
echo "chain3_rc=$?" >> "$LOG/chain3.log"

# E. chains 1-2
stage "E chain1+chain2"
bash "$WT/.planning/r28-t05-workflow-teaching-delivery/chain1_teaching.sh" > "$LOG/chain1.log" 2>&1
echo "chain1_rc=$?" >> "$LOG/chain1.log"
bash "$WT/.planning/r28-t05-workflow-teaching-delivery/chain2_experiment.sh" > "$LOG/chain2.log" 2>&1
echo "chain2_rc=$?" >> "$LOG/chain2.log"

# F. fast-lane classification (the 77-failure re-measure) — longest stage
stage "F fast-lane classification"
ctest --test-dir "$BUILD" -j1 -LE "SLOW|LONG_RUN" -E "NOT_BUILT" \
  > "$LOG/fastlane.log" 2>&1
echo "fastlane_rc=$?" >> "$LOG/fastlane.log"

stage "DONE"
for f in "$LOG"/*.log "$LOG"/*.txt; do echo "--- $f"; tail -3 "$f" 2>/dev/null; done
