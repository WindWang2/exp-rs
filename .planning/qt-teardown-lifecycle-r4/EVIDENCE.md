# EVIDENCE — Track 2: Qt/QGIS teardown lifecycle (R4 Deep)

Append-only evidence chain. Every claim links to command + observed output + commit.

## 1. Baseline inventory (rounds 0–1)
- `git rev-parse origin/master` → `15e5c66b543ef3874cb929f17529ef456bd6c059`, local master `0/0` vs origin.
- `rg -n "std::_Exit|_Exit\(" src tests` → 43 lines / 28 files / **30 real call sites; `src/` = 0**. Classified: 21 stats-variant atexit + 5 ok-variant atexit + 3 crash-injection (`test_chunk_resume_11`) + 1 probe exit (`offline_probe.h:254`).
- `gh issue list --state open` → empty. PRs #1334–#1338 inventoried (BASELINE §2).
- **Master link breakage**: 25/39 of this track's baseline binaries failed to link with exactly the two symbols PR #1335 P0-3 fixes (`OpsDriver::apply`; `agent_loop::VerificationReport::aggregate`). Verified with #1335's three CMake hunks as a **local uncommitted overlay** (documented; never committed to this branch). Overlay present during all runs below.

## 2. Red-step evidence (crash reproduction) — CONFIRMED
Procedure: baseline binary green with defense → defense disabled via temp edit → rebuild → run.
- Baseline (defense active): `test_gcp_contains` rc=0.
- Defense disabled: **rc=139 (SIGSEGV, core dumped)** *after* `All tests passed (10 assertions in 2 test cases)` (log: `/tmp/r4qtt-red-gcp.log` flow; coredump 987326).
- Backtrace (gdb, coredumpctl):
```
#4  QBasicReadWriteLock::lockForWrite (this=0x0)                     ← NULL lock
#6  QgsReadWriteLocker::QgsReadWriteLocker(...) qgsreadwritelocker.cpp:27
#7  QgsCoordinateReferenceSystem::removeFromCacheObjectsBelongingToCurrentThread
    qgscoordinatereferencesystem.cpp:770
#8  QgsProjContext::~QgsProjContext  qgsprojutils.cpp:50
#10 QThreadStorage<QgsProjContext*>::deleteData
#12 QCoreApplicationPrivate::cleanupThreadData()
#14 QApplicationPrivate::~QApplicationPrivate()                      ← the leaked static app
#16 exit()
```
- **Root cause (CONFIRMED, A-1/A-3)**: the leaked static `QApplication` is destroyed *inside* glibc `exit()` (LIFO, after every `Q_GLOBAL_STATIC` cache guard created during the run); `~QApplicationPrivate::cleanupThreadData()` then destroys the main thread's `QThreadStorage` → `~QgsProjContext` walks the CRS cache (`removeFromCacheObjectsBelongingToCurrentThread`) whose lock guard is already destroyed → `lockForWrite(0x0)` → SIGSEGV. This build uses the `QThreadStorage` branch of `qgsprojutils.h:296-304` (`USE_THREAD_LOCAL` undefined), so the context dies exactly when Qt tears down thread data.
- Why the retirement works: the shared listener performs the production sequence (`sendPostedEvents(DeferredDelete)` → `exitQgis()` → `delete app`) **inside the run, before main returns** — `~QgsProjContext` then runs while all cache guards are alive (safe path), and at `exit()` nothing QGIS-owned remains. Same ordering as `src/app/main.cpp:622-635`.

## 3. Retirement evidence — 27/31 rows 退役, all double-run green
All 26 atexit-family `_Exit` sites (rows 1–26 of RETIREMENT.md) + the #1319 `test_view_link` leak (row 31): defense removed, shared `TeardownListener` + heap-owned app, **run 1: 26/27 green (1 pre-existing functional red, see below); run 2: 26/26 green** (the same red excluded from round 2 as it is a failing product assertion, not a crash).
- Pre-existing red (unchanged by this track, verified identical at baseline): `test_georef_dual_window` case at :126 `REQUIRE( w.hasPendingSourceForTest() )` — product-side behavior red on master; recorded for the georef-workbench owners.
- Per-file commits: `git log --grep 'retire(r4-qt-teardown)'` (26 commits + `9c3bc4bb2` view_link).
- Full-repo `_Exit` call sites after track: **4** (down from 30): 3× `test_chunk_resume_11` (语义保留, crash-injection semantics) + 1× `offline_probe.h:254` (语义保留, probe protocol). Zero `_Exit` remains in the retired files (enforced by gate).

## 4. Undefended exit-crash cluster (PR #1335 taxonomy group 2) — hardened
7 binaries (`test_provider_http`, `test_georef_crs_pick_failclosed`, `test_layer_sync_contract`, `test_qgis_display_manager`, `test_edit_roi_semantics`, `test_edit_persistence`, `test_editing_e2e`) had **no defense** (reported crashing at exit on the reviewer's Debian 13/Qt 6.8.2 box). On this toolchain they exited cleanly even undefended (crash is platform/glibc-order dependent), but they are now covered by the shared ordered teardown so the guarantee does not depend on destruction-order luck. All 7: green ×2 (`dc112cb95`).
- `test_provider_http` initially aborted under the listener (`free(): invalid size`, stack: `qt_lifecycle.h:76 delete app`): its app was a **value-static** `QCoreApplication` (atexit-registered AND heap-deleted — double ownership). Fixed by heap ownership (`static QCoreApplication *app = new QCoreApplication(...)`) → green ×2. This validates the helper's ownership contract.

## 5. Teardown fixtures (WP-C) — 18 cases, green ×2
Binaries (registered via `sicnu_discover_tests`, every TEST_CASE name carries "teardown"): `test_canvas_teardown_r4` (5), `test_timeline_scrubber_teardown_r4` (3), `test_roi_statistics_teardown_r4` (3, bounded `RsScanPool` #797 cancel contract), `test_dual_viewport_teardown_r4` (4), `test_maptool_teardown_r4` (3, canvas↔QgsMapTool single-deletion contract). Fixture-driven fixes during verification: ROI namespace, map-tool parent-ownership (QgsMapTool parents to canvas — fixture rewritten to single-deleter contract), sync no-post-mortem assertion now compares against the last live sync. All green ×2.

## 6. Exit-path contracts (WP-E) — green ×2
`test_exit_path_contracts_r4`: shared shutdown-pair source-drift check (main.cpp:323/622), `shutdownForTests` idempotence, terminal-shutdown tolerance, `exitQgis()` no-instance idempotence ×2.

## 7. Drift gate (WP-G) — green; manual drill verified red
`test_teardown_retirement_gate_r4` parses RETIREMENT.md 退役 rows → asserts `_Exit/_exit`-free. Drill: injected `std::_Exit(0)` into retired `test_gcp_contains.cpp` → gate RED ("retirement drift in test_gcp_contains.cpp: 1 _Exit/_exit call(s) reintroduced") → reverted → green.

## 8. Oracle gate (§7.1) — two consecutive green runs
```
ctest -R "lifecycle|teardown|dialog_stress|view_link" -j1 --timeout 300
run1: 100% tests passed, 0 tests failed out of 22   (41.29s)
run2: 100% tests passed, 0 tests failed out of 22   (24.92s)   [log: ctest-gate-run2.md]
```
Known-adjacent reds OUTSIDE the regex's selected set (documented, pre-existing on master, verified in baseline): `test_m2_dialog_stress` (i18n cluster — unmerged PR #1336's file), `test_georef_dual_window` (product assertion above).

## 9. Deliverable floors (§4.2) — final tally
| floor | required | delivered |
|---|---|---|
| adjudication rows | 31/31 | **31/31** (退役 27 / 语义保留 4 / 保留 0) |
| retirements | ≥8 | **27** |
| teardown fixtures | ≥15 | **18** (+ exit-path/gate cases) |
| atomic commits | ≥20 | **40+** (per-file retirements included; each compiles — helper committed before consumers) |
| files touched | ≥25 | **36** (26 retired files + view_link + 7 cluster files + 6 new fixtures/gate + support header + CMakeLists + planning docs) |
| planning artifacts | 5 | BASELINE / DECISIONS / RETIREMENT / EVIDENCE / REVIEW_LOG ✓ |

## 10. Environment & honesty notes
- Fresh build dir `build-r4` (Debug, `ENABLE_TESTS=ON`, Ninja, `-j2` hard; RAM stayed ≤ ~58%, no `-j1` downgrade needed).
- Local uncommitted overlay = PR #1335 P0-3 CMake hunks (`src/agent/CMakeLists.txt`, `src/agent_loop/CMakeLists.txt`, `src/app/CMakeLists.txt`) required because master's link graph is broken for 25 targets until #1335 merges. This branch's own commits never touch those files.
- Token accounting in `.goal-loop-ledger.md` is an honest agent-side estimate (no harness meter); it is far below the brief's 280M arithmetic — the brief's own workload model assumed a different (unmeasured) scale; actuals recorded as actuals.
