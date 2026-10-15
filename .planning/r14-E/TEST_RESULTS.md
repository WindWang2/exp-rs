# R14-E Test Results (real runs, real exit codes)

Environment: worktree `../exp-rs-r14-E-teaching-evidence`, build dir `build-r14-E`
(Ninja, Debug, Qt 6.12 / GCC / Catch2 3.7.1, SDK prefix /home/kevin/pwb-sdks/root/usr,
catch2 sourced from build-review's v3.7.1 tree via FETCHCONTENT_SOURCE_DIR_CATCH2 —
offline). Builds: `CMAKE_BUILD_PARALLEL_LEVEL=2`, repo resource_guard per build dir,
cross-track `/tmp/exp-rs-r14-global-build.lock` for full-target builds. Tests:
`ctest -j1`, `QT_QPA_PLATFORM=offscreen`. Track D's parallel build held the shared lock
during the light ctest-only runs 2/3 (seconds of -j1 test load) — those ran without the
global lock, documented here.

## RED evidence

| Fix | RED against pristine master production code | Result |
|---|---|---|
| O1 studio model (WP08) | syntax-only compile of new test TU with stashed src | compile error: `RunMatrixTableModel is not a member of sicnu::app` (log preserved in session) |
| O2 symlink containment | master batch_assessment.cpp + new test | `REQUIRE( skips.size() == 3 )` → 2 (link followed / vanish) — FAILED |
| O3 NFC ids | master + new test | NFD row carried NFD id — FAILED |
| O4 bounded reads | unit test of readBounded (additive header) | passes on master too — API-shape oracle, behavioral RED consciously skipped (a 64 MiB oversize fixture was judged too heavy for CI; call-site degradation is typed in code) |
| O5 lab seq envelope | master session_state.cpp + new SECTIONs | `REQUIRE_FALSE(sessionFromJson.ok)` failed — document adopted — FAILED |
| O6 faultlab Changed vacuous | master fault_expectations.cpp + new test | `CHECK_FALSE(results[0].passed)` failed (vacuous pass) — FAILED |
| O7 LO grammar | master fault_scenario.cpp + new test | bad ids accepted — FAILED (2 assertions) |
| O8 sidecar non-finite | master model_sidecar.cpp + new test | parse succeeded — FAILED |
| O9 preflight unknowns | master rules.cpp + new test AND updated golden pins | both FAILED |
| O10 router race | smoke test (public API) | race itself is TSan-lane territory; smoke passes on master by design — declared |

## GREEN evidence (×2 on final code + 1 confirm run after RED restore)

`ctest -R "^(test_teaching_batch_discovery_r10|test_lab_runtime|test_faultlab$|test_scientific_state_provenance|test_preflight_rules|test_preflight_golden|test_science_context_broker|test_experiment_studio_dock)" -j1`

- Run 1: **84/84 passed** (7.45 s)
- Run 2: **84/84 passed** (7.34 s)
- Post-RED-restore confirmation: **84/84 passed** (7.19 s), tree == HEAD
- AFTER the WP08 dedupe drop (PR #1510 collision; studio dock reverted to master),
  definitive suite = 7 targets / **76/76 passed** — final numbers below in this file.

## Performance measurement (WP08 oracle — implementation dropped for #1510; data retained)

- 10,000-row StudyReport → projectRunMatrix → RunMatrixTableModel::setRows + QTableView
  resize/show + processEvents: **19 ms** measured (`populateMs` CAPTUREd,
  Catch2 -s output). Structural oracle: zero QTableWidget in the dock; the old path
  allocated 6 QTableWidgetItem per row (60k heap objects at 10k rows).
- No other performance claims are made; nothing else was optimized.

## Suite-level facts

- 8 test executables, 84 ctest cases (studio dock 8; lab_runtime, faultlab, discovery,
  provenance, rules, golden, broker suites cover the rest), all FAST-labeled.
- `test_faultlab_params_r8_NOT_BUILT` appears as NOT_BUILT in ctest listings because only
  selected targets were built in this worktree build dir (pre-existing naming; not a
  regression — it is unbuilt by target selection, and its source is untouched).
- Full-build compile: 0 errors after the round-1 fixes (one P0 compile error was found by
  review and fixed BEFORE any GREEN claim; the earlier link failures during bring-up were
  the invalid findChildren<QTableWidgetItem*> call (QTableWidgetItem is not a QObject),
  a missing include, FaultResult member-name misuse, and a suppressed default ctor — all
  fixed with real compiler evidence in session logs).

## DEFINITIVE final run (post-dedupe delivered tree, 2026-10-15)

7 targets / **76/76 passed** (4.14 s), after rebuilding the studio dock TU back to master
sources (PR #1510 owns that file now). Exit code 0.
