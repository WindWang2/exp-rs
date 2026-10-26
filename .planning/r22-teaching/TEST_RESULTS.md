# TEST_RESULTS — r22-teaching round 1

Host: Windows 10 (win32), MSVC 14.38 (manual env, vcvars blocked), Qt 6.8.0 msvc2022_64,
Ninja, Debug, ENABLE_TESTS=ON, build dir `build-r22-teaching` (own; never shared).
Dependencies consumed classic-mode from the shared tree
`C:/Users/wangj.KEVIN/projects/exp-rs/build-dev/vcpkg_installed/x64-windows`
(GDAL_DIR/PROJ/GEOS/OpenCV_DIR/CMAKE_PREFIX_PATH explicit) after a vcpkg-manifest
ABI-rebuild storm was aborted and the tree repaired (opencv4+protobuf reinstalled 8.5 min,
see LOOP_LEDGER R2/R3). Test exes run with DLLs deployed beside them in the build dir
(Qt6Cored, sicnu_runtime, sqlite3d, jsoncppd debug variants).

## RED (commit 1610c9b75, master product code)

| suite | result | failing evidence |
|-------|--------|------------------|
| test_r22_teaching_batch_regrade | 1/2 cases failed | `#1694` rules-edit: `REQUIRE(report2.resumed == 0)` → `2 == 0` (stale adoption) |
| test_r22_teaching_guard_hygiene | 3/5 cases failed | `#1678` NaN accepted (`!true`); `#1689` numeric foreign id → guard ok; `#1688` `1.0&calc` → program `cmd.exe` |

#1678 RED ran against a TEMPORARY LOCAL uncommitted `#include <algorithm>`
(the master-wide #1696 MSVC breakage; infra lane owns it). The include is NOT in any commit.

## GREEN (final, after R1+R2 review fixes: 572068233 + 318b7d0c8) — key suites double-run

| suite | pass 1 | pass 2 |
|-------|--------|--------|
| test_r22_teaching_batch_regrade (10 cases) | 183/183 assertions | 183/183 |
| test_r22_teaching_guard_hygiene (8 cases) | 107/107 assertions | 107/107 |

Intermediate GREEN at 70afbb6a8 was 98+76 (6+7 cases) — superseded by the review-fix rounds
which added the 8 R1 test-gap cases and 4 R2 regression/hermeticity cases.

## Affected existing families

| suite | result | classification |
|-------|--------|----------------|
| test_teaching_admin_checkpoint_stamp_r16_c_t06 | 85/85 PASS (×2) | digest change did NOT break legitimate resume (unchanged authority still adopts) |
| test_teaching_admin_grader_cli_budget_r16_c_t06 | 3/3 PASS | — |
| test_teaching_admin_core (37 cases) | 35 PASS, 2 FAIL | BOTH FAIL AT RED BASELINE TOO (stash-verified, same assertions) and IDENTICAL after the R2-fix commit (318b7d0c8): (a) `gradeViaCli … grader_crashed` — Windows MSVC abort() is a normal exit(3), not a signal, so the fake CLI's crash simulation lands on the exit-3 unverifiable path; (b) `script adapters default to the harness-pinned interpreter` — python pin probe on this host. KNOWN_BASELINE_FAIL (env/platform), not regressions. R2's P1 sentinel regression was caught by re-running this suite and is fixed (back to exactly the baseline 2). |
| test_lab_runtime (25 cases) | 24 PASS, 1 FAIL | `session store persists atomically…` throws in `std::filesystem::remove_all` (file-in-use on the temp dir) — Windows handle race in teardown, mechanism unrelated to the numeric-validation edits (which pass in guard_hygiene). Deterministic on this host. KNOWN_BASELINE_FAIL (env). |

## NOT_EXECUTED / caveats

- Full-repo test sweep not run (host budget); targeted families above cover every
  touched product area: batch orchestration, grader CLI adapter, feedback pack,
  script adapters, lab session state.
- sicnu_lab_runtime CANNOT build from a clean checkout of this branch on MSVC until
  infra lane's #1696 `<algorithm>` fix lands (pre-existing master breakage; my
  session_state.cpp edits are guarded instead by the temp-include build + tests above).
- Dock UI wiring (onRunBatch/onBuildFeedbackAndSummary/finishBatchOnUiThread) verified
  by TU compile of teaching_admin_dock.cpp (see below) + code review; no GUI runtime test
  infrastructure for this dock exists in-repo.
- CI not awaited by policy.

## Dock TU compile

`cmake --build build-r22-teaching --target src/app/CMakeFiles/sicnu_geo_rs_shell.dir/teaching_admin/teaching_admin_dock.cpp.obj`
- full dependency chain (1624/1624 objects, incl. sicnu_processing/core/lab_runtime under the
  documented temp #1696 include): **exit 0** (log .dock_tu4.log)
- incremental rebuild after the R2 dock changes (1212 TUs recompiled against the new
  teaching_admin headers): **exit 0** (log .dock_tu5.log)
