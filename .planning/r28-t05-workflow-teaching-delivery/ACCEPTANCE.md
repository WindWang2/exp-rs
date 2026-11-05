# ACCEPTANCE — R28-T05

All evidence rows below are filled ONLY from real local runs in this worktree
(build-r28-t05, Debug + tests + OpenCV5 prefix + ccache, JOBS=2 under the
shared host lock). No number is copied from a PR description.

## Gates

| Gate | Command | Status | Evidence |
|---|---|---|---|
| G1 Configure | `cmake -S . -B build-r28-t05 …` (guard-wrapped) | PENDING | /tmp/r28t05-configure.log |
| G2 Build (track targets) | `cmake --build build-r28-t05 --target <track tests + libs>` | PENDING | |
| G3 New regressions green ×2 | `ctest --test-dir build-r28-t05 -R "test_r28_t05" -j1 --output-on-failure` (run twice) | PENDING | |
| G4 Track suites baseline+fixed | ctest -R "<track suites>" on baseline SHA vs fixed branch | PENDING | |
| G5 CTest inventory | `ctest --show-only=json-v1` exported; count ≠ 0 | PENDING | |
| G6 77-failure re-measure | full/fast ctest classification vs #1812's claim | PENDING | |
| G7 Snapshot regeneration | `surface_snapshot_regen` + drift suites after build | PENDING | |
| G8 Real install | `cmake --install` → staging prefix → CLI smoke with build-tree env cleared | PENDING | |
| G9 Offline bundle | build_offline_bundle.sh → verify → run lab1 in fresh dir → grade → hash check | PENDING | |
| G10 Joint chains ×3 | teaching chain / experiment+repro chain / install-tree chain (each incl. one error-cancel + one recovery scenario) | PENDING | |
| G11 Merge integrity | `scripts/check_merge_registration_integrity.py` + reconfigure after master merge | PENDING | |
| G12 R1/R2 reviews | independent reviewer reports | PENDING | |

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
