# BASELINE — Track 2: Qt/QGIS teardown lifecycle closure (R4 Deep Edition)

Track branch: `hardening/r4-qt-teardown` · worktree: `/home/kevin/project/exp-rs-qt-teardown-r4`
Recorded: 2026-09-27 ~00:50 local. All numbers below are **measured on this machine**, not inherited from the brief.

## 1. Measured git baseline
- `origin/master` = `15e5c66b543ef3874cb929f17529ef456bd6c059` (same as brief's writing-time SHA; brief warned it may have moved — it hasn't as of record time, re-check before PR).
- local `master` vs `origin/master`: `0 / 0` (in sync).
- Worktree created as first mutating action, per iron rule; repo root (master checkout) untouched.

## 2. Open PR inventory (gh, measured)
| PR | files | whitelist-relevant overlap | impact |
|---|---|---|---|
| #1334 fix/review-p1-security | 30 | `src/app/main.cpp`, `src/app/main_window.cpp`, `tests/CMakeLists.txt` (+non-whitelist) | WP-A seam overlap; my main.cpp work is audit/analysis → low conflict; tests/CMakeLists.txt append-only policy |
| #1335 fix/review-p0-build-restore | 19 | `src/app/CMakeLists.txt`, `tests/test_view_link.cpp`, `tests/CMakeLists.txt` | **test_view_link overlap**: #1335 restores #1319 leak + adds `_Exit` on its branch. Master (my base) has leak-only. My root-cause retirement supersedes; rebase note written in PR |
| #1336 fix/hardening closure-ui-runtime-r4 | 13 | `tests/test_m2_dialog_stress.cpp` + i18n/help files | no overlap with my 28 `_Exit` files; my gate includes `dialog_stress` regex → baseline state checked in §7 |
| #1337 closure-workflow-contracts-r4 | 25 | tests (workflow/data-parse/preflight family) | none of my files |
| #1338 closure-io-processing-r4 | 29 | tests (io/vector family) | none of my files |

## 3. Review materials consulted
- `PROJECT_REVIEW_DOSSIER_5.0.md`, `AUDIT_DOSSIER_ISSUES_747_760.md`, `PR_TRIAGE_REPORT_2026-09-16.md`, `docs/PARALLEL_TRACKS_10.md`: **absent on master** (brief anticipated this). Authoritative substitutes: full bodies of PR #1335 (P0 classification, 142-failure taxonomy) and PR #1336 (cluster A unresolved-item disclosure + Windows/Linux teardown asymmetry evidence).
- Key cross-track facts from #1335: master has **~14 tests that pass all assertions then segfault at exit, with no defense**: test_provider_http(5), georef_crs_pick_failclosed(3), workspace_services, layer_sync_contract, qgis_display_manager, edit_roi_semantics, edit_persistence, editing_e2e. These are in-scope root-cause-fix candidates (tests/ is whitelisted).

## 4. Open issues
`gh issue list --state open` → **0** (matches brief).

## 5. Boundary declaration (whitelist)
Allow: `src/app/`, `src/gui/`, `src/ui/`, corresponding `tests/` (lifecycle fixtures + `_Exit` host files + `tests/CMakeLists.txt` append-only), `.planning/qt-teardown-lifecycle-r4/`.
Forbidden: `src/core/` product refactors (read-only reference only — incl. defensive comments at `src/core/proj/qgsellipsoidutils.cpp:348`, `src/core/proj/qgscoordinatereferencesystem.cpp:3329`), new features/operators/workbenches, waiting on CI, other tracks' worktrees, shared snapshot files not owned here.

## 6. `_Exit` full inventory (measured; supersedes brief §4.1 anchors)
`rg -n "std::_Exit|_Exit\(" src tests` → **43 matching lines, 28 files, 30 real call sites; `src/` = 0**.

**Family A — Catch2 `FastExitListener` atexit defense (26 files / 26 sites)**
- `stats.aborting || totals.testCases.failed > 0 ? 1 : 0` variant (21): test_classification_window:38, test_dual_viewport_sync:30, test_gcp_canvas_crs:30, test_gcp_canvas_item:28, test_gcp_contains:25, test_gcp_list_widget:26, test_georef_dual_window:28, test_georef_dual_window_workbench:38, test_georeferencing_session:35, test_georef_session_adapters:20, test_georef_task_list:20, test_georef_window:30, test_georef_window_rpc_mode:32, test_georef_window_warp_lock:21, test_hooks_comprehensive:43, test_layout_designer:52, test_layout_tools:60, test_pick_canvas_mode:25, test_rms_scatter:26, test_spectral_curve:24, test_twincanvas_sync:30
- `ok ? 0 : 1` variant (5): test_layout_designer_lifecycle:47, test_project_context_run_mirror:58, test_project_session_boundary:53, test_secondary_map_view_session:56, test_workbench_full_shell_lifecycle:81
- Uniformity scan: 23/26 carry `ensureApp()` helper with function-local `static QApplication` (leak-until-exit); 3 files (test_layout_tools, test_georef_session_adapters, test_georef_dual_window) use another app-establishing path — confirmed during WP-B.

**Family B — hard process-death injection (semantic-retain): test_chunk_resume_11.cpp:101,115,128 (3 sites)**

**Family C — probe helper: tests/support/offline_probe.h:254 `std::_Exit(77)` (semantic-retain)**

**Leak precedent (31st adjudication point): tests/test_view_link.cpp:84** — `static QgsApplication app(...)` never destroyed (+`initQgis()`/`exitQgis()`); `main()` at :79. No `_Exit` on master (#1335 adds one on its unmerged branch).

## 7. Baseline test expectations (to be measured once build completes)
Oracle gate: `ctest -R "lifecycle|teardown|dialog_stress|view_link" -j1` ×2 green in a fresh build dir with `ENABLE_TESTS=ON`.
- Baseline reds expected: gate binaries with atexit defense should be green; `test_view_link` state on master (leak-only) to be measured; `test_m2_dialog_stress` may be red on master (i18n drift, fixed in unmerged #1336) — if red for non-teardown reasons, record and coordinate, do not mask.
- Build: fresh `build-r4`, Ninja, Debug, `ENABLE_TESTS=ON`, `-j2` hard limit, offscreen Qt. First build = 39 targets (27 `_Exit` binaries + view_link + 11 exit-crash/gate binaries).

## 8. Environment facts
- cmake: `/home/kevin/toolchain/cmake-dist/bin/cmake` (3.30.5) · ninja: `/home/kevin/pwb-sdks/root/usr/bin/ninja` (1.12.1; shell `ninja` alias forces `-j40` — **always use absolute path**) · c++: GCC 16.2.1 · Qt6: system `/usr` · prefix: `/home/kevin/pwb-sdks/root/usr` · ccache: absent.
- **~18 sibling R4 tracks run concurrently on this host** (observed via ps). Consequences: (a) never use shared `/tmp` log names — prefix `r4qtt-`; (b) `-j2` is also a civility bound toward sibling tracks; (c) before every commit, `git status` audit for foreign writes into the worktree (one stale foreign ledger was found and archived to /tmp at round 0).
