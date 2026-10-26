#!/usr/bin/env bash
# R21-E staged RED->GREEN chain v2 (self-managing stash; idempotent).
set -uo pipefail
WT=/home/kevin/project/exp-rs-r21-E-20261023T215154Z
cd "$WT"
export QT_QPA_PLATFORM=offscreen
export CMAKE_BUILD_PARALLEL_LEVEL=4
LOG="$WT/.planning/r21-E/redgreen_chain.log"
: > "$LOG"
log() { echo "[$(date -u +%H:%M:%S)] $*" | tee -a "$LOG"; }

BASE_TARGETS="test_r21_e_verifier_evidence test_r21_e_batch_stress"
GREEN_TARGETS="test_r21_e_verifier_evidence test_r21_e_faultlab_verifier test_r21_e_batch_stress test_r21_e_studio_verifier_hook sicnu_geo_rs_cli sicnu_generate_samples"

need_stash() {
  git diff --quiet -- src/agent/output_verifier.h src/agent/output_verifier.cpp \
    src/agent/CMakeLists.txt src/verify_adapters/CMakeLists.txt && echo no || echo yes
}

log "stage 0: stash state = $(need_stash)"
if [ "$(need_stash)" = "yes" ]; then
  git stash push -m "W1 production code (RED pending v2)" -- src/agent src/verify_adapters >> "$LOG" 2>&1
  log "  W1 stashed for BASE"
else
  log "  W1 already at BASE"
fi

log "stage 1: BASE build"
flock -x /tmp/exp-rs-r21-build.lock -c "cmake --build build-r21-E --target $BASE_TARGETS -j4" >> "$LOG" 2>&1 \
  || { log "FATAL: BASE build failed"; exit 1; }

log "stage 2: RED runs at BASE"
QT_QPA_PLATFORM=offscreen ./build-r21-E/tests/test_r21_e_verifier_evidence > "$WT/.planning/r21-E/RED_verifier_evidence.txt" 2>&1
log "  evidence RED exit=$? (expect nonzero)"
QT_QPA_PLATFORM=offscreen ./build-r21-E/tests/test_r21_e_batch_stress > "$WT/.planning/r21-E/RED_batch_stress.txt" 2>&1
log "  stress exit=$?"

log "stage 3: restore W1 production code"
git stash pop >> "$LOG" 2>&1 || { log "FATAL: stash pop failed"; exit 1; }

log "stage 4: reconfigure"
cmake -S . -B build-r21-E \
  -DNETCDF_INCLUDE_DIR=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr/include \
  -DNETCDF_LIBRARY=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr/lib/x86_64-linux-gnu/libnetcdf.so \
  -DZLIB_LIBRARY_RELEASE=/usr/lib/libz.so >> "$LOG" 2>&1 || { log "FATAL: reconfigure"; exit 1; }

log "stage 5: GREEN build"
flock -x /tmp/exp-rs-r21-build.lock -c "cmake --build build-r21-E --target $GREEN_TARGETS -j4" >> "$LOG" 2>&1 \
  || { log "FATAL: GREEN build failed"; exit 1; }

log "stage 6: GREEN runs (x2)"
for round in 1 2; do
  for t in test_r21_e_verifier_evidence test_r21_e_faultlab_verifier test_r21_e_batch_stress test_r21_e_studio_verifier_hook; do
    QT_QPA_PLATFORM=offscreen "./build-r21-E/tests/$t" > "$WT/.planning/r21-E/GREEN_r${round}_${t}.txt" 2>&1
    log "  r$round $t exit=$?"
  done
done

log "stage 7: e2e offline lab"
bash "$WT/.planning/r21-E/e2e_offline_lab.sh" "$WT/build-r21-E" > "$WT/.planning/r21-E/E2E_RUN.txt" 2>&1
log "  e2e exit=$?"

log "CHAIN DONE"
