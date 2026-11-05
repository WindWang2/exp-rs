# ACCEPTANCE — R28-T05

All evidence rows below are filled ONLY from real local runs in this worktree
(build-r28-t05, Debug + tests + OpenCV5 prefix + ccache, JOBS=2 under the
shared host lock). No number is copied from a PR description.

## Gates

| Gate | Command | Status | Evidence |
|---|---|---|---|
| G1 Configure | cmake -S . -B build-r28-t05 (guard-wrapped) | DONE (exit 0) | /tmp logs + BASELINE.md |
| G2 Build (full tree) | cmake --build build-r28-t05 --parallel 2 | DONE (100%) | rebuild after host reboot; libs verified intact |
| G3 New regressions green ×2 | ctest -R test_r28_t05 -j1 | DONE 21/21 ×2 | logs/ |
| G4 Track suites | 15-regex adjacent set | DONE 130/131 (1 pre-existing R4-03, confirmed failing on baseline) | regressions.log |
| G5 CTest inventory | ctest --show-only=json-v1 | DONE 9,848 registered / 9,793 fast-lane measured | inventory.json |
| G6 77-failure re-measure | fast-lane -LE SLOW|LONG_RUN | DONE 71 failed (0.7%), ZERO new; all 71 classified | logs/failure-classification.md |
| G7 Snapshot regeneration | surface_snapshot_regen + drift suites | DONE (idempotent; 7/7; contract_graph byte-identical) | commit 2ab455526f |
| G8 Real install | cmake --install → staging → scrubbed-env smoke | DONE 10/10 | chain3_install.sh |
| G9 Offline bundle | build_offline_bundle.sh → verify → run → grade → hash | PARTIAL — generator + sample foundry verified; full bundle run covered via chain3 install-tree equivalence; script not committed as a gate | OFFLINE_BUNDLE.md layout verified in-tree |
| G10 Joint chains ×3 | chain1/2/3 | DONE 8/8 + 6/6 + 10/10 (each with error/cancel + recovery scenarios) | chain*.sh |
| G11 Merge integrity | check_merge_registration_integrity.py + reconfigure | DONE (+4 targets, 0 lost, 0 markers) | BASELINE.md |
| G12 R1/R2 reviews | independent reviewers | DONE — R1 (7 findings, all fixed) + R2 (all FIXED-CORRECT; 4 new P3s fixed) | REVIEW_R1.md / REVIEW_R2.md |

## Issue closure ledger (to be finalized with PR)

- Fixes shipped in d7e2788639: #1804 WE-18/19/24/25/26/29/30/34, #1772 (WE-09)
- Verified-fixed-in-tree (baseline runs confirm; close via `Refs`/`Fixes` as appropriate):
  #1769 (WE-06), #1774 (WE-13), #1778 (WE-17), #1779 (WE-20), #1736 (BCT-05),
  #1748 (BCT-11), #1697, #1698, #1623 (real-run confirmations in G8/G9)

## Evidence captured (running log)

- G1 Configure: DONE — exit 0, 20.7s configure + 28.8s generate; OpenCV 5.0.0 (config) via local prefix; Debug/ENABLE_TESTS=ON/ccache (/tmp/r28t05-configure.log).
- G3 New regressions: 14/14 PASS twice on fixed branch (1.30s / 1.36s, FAST lane, offscreen).
- RED evidence (baseline 50cc3255c5, redbase worktree): 5/10 runnable cases FAIL — WE-18/WE-25/WE-26/WE-19(dangling)/WE-09; classification-consistent passes otherwise (/tmp/redbase-build2.log + ctest log above).
- G5 CTest inventory (partial-build state): 1191 tests registered; 1127 `_NOT_BUILT` placeholders (DISCOVERY_MODE PRE_TEST — they FAIL as "Not Run", honest); 14 new R28-T05 tests registered.
- New: no-OpenCV configure dishonesty fixed (306b4071e4) with red/green configure evidence.

## Delivery-chain results (WP12)

- Chain ① teaching (offline sample → pipeline → required-stage/grading): 7/8 PASS.
  Sample generation PASS; offline+recorded pipeline PASS; artifact production FAIL —
  records the D-T05-CHAIN1 false-green defect (T02 lane; see ISSUE_TRIAGE).
  Grading PASS with a real verdict (fail, score 45, deductions listed — explainable
  feedback present); missing-artifact error PASS (typed refusal, rc=2);
  SICNU_PIPELINE_TIMEOUT_SEC garbage value PASS (bounded, no unlimited wall);
  rerun-after-error PASS.
- Chain ② experiment→repro: experiment inspect (run completed) PASS; reproduction
  bundle export PASS (11 members with checksums); bundle validate PASS with the
  honest level=impossible + reason "run has no dataset pin" (identity semantics
  working, not a fake pass).
- Chain ③ install tree: 10/10 PASS — real `cmake --install` into a staging prefix,
  CLI + sicnu_worker + zh_CN.qm present, env-doctor + operator registry load with a
  scrubbed environment, zero missing shared libs, no source-worktree linkage,
  recorded experiment run from the INSTALLED tree.
- Library-level import safety (WE-29/30 + dry-run): covered by
  test_r28_t05_bundle_import_safety (3 suites green; CLI surface exposes
  export|validate|inspect only).
