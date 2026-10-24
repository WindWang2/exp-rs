#!/usr/bin/env bash
# R21-E regression sweep — build + run the in-track families twice (GREEN×2).
set -uo pipefail
WT=/home/kevin/project/exp-rs-r21-E-20261023T215154Z
cd "$WT"
export QT_QPA_PLATFORM=offscreen
export SICNU_LAB_RULES_DIR="$WT/data/labs/grading"
export SICNU_OFFLINE=1
OUT="$WT/.planning/r21-E/TEST_RESULTS.md"

TARGETS=(
  test_r21_e_verifier_evidence test_r21_e_faultlab_verifier test_r21_e_batch_stress
  test_r21_e_studio_verifier_hook
  test_lab_grading test_lab_batch test_lab_batch_classroom_safety test_lab_scale
  test_lab_offline_e2e test_lab_data_pack test_lab_batch_v2 test_lab_grader_kernels
  test_output_verifier test_harness_lab_evals test_harness_lab_injection
  test_grader_adapters test_grader_engine test_grader_render test_grader_schema
  test_r16_a_grader test_r16_a_faultlab test_faultlab test_fault_matrix
  test_faultlab_params_r8 test_r18_e_grader_faultlab test_r18_e_verifier_rules
  test_r18_e_batch_cli test_evidence_seam test_verifier_engine test_verifier_adversarial
  test_verifier_levels test_verifier_numeric test_verifier_packs test_verifier_provenance
  test_verifier_render test_verifier_schema
  test_teaching_foundation_e2e test_teaching_fake_grader_cli test_teaching_restore_r16_c_t05
  test_lab_session_store_r16_c_t05
  test_studio_live_e2e
  test_help_core test_r18_e_help_contracts
)

pass1_summary=""
for round in 1 2; do
  echo "=== sweep round $round: $(date -u +%H:%M:%S) ==="
  pass=0; failed=0; skip=0
  : > "/tmp/r21e_sweep_r${round}.txt"
  for t in "${TARGETS[@]}"; do
    bin="build-r21-E/tests/$t"
    if [ ! -x "$bin" ]; then echo "SKIP(no binary) $t" | tee -a "/tmp/r21e_sweep_r${round}.txt"; ((skip++)); continue; fi
    if "$bin" > "/tmp/r21e_one_${t}.txt" 2>&1; then
      echo "PASS $t" >> "/tmp/r21e_sweep_r${round}.txt"; ((pass++))
    else
      echo "FAIL $t (exit $?)" | tee -a "/tmp/r21e_sweep_r${round}.txt"
      cp "/tmp/r21e_one_${t}.txt" "$WT/.planning/r21-E/SWEEP_FAIL_${t}_r${round}.txt" 2>/dev/null
      ((failed++))
    fi
  done
  echo "round $round: pass=$pass fail=$failed skip=$skip"
  [ "$round" = "1" ] && pass1_summary="pass=$pass fail=$failed skip=$skip"
done
echo "SUMMARY r1: $pass1_summary / r2: pass=$pass fail=$failed skip=$skip"
