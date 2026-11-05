# RESUME — R28-T04 (read this first when resuming)

- **Branch**: `agent/r28-t04-workbench-cartography-20261104T210902Z`
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r28-t04-workbench-cartography-20261104T210902Z` (build dir `build-r28-t04`)
- **BASE_SHA**: `50cc3255c5eb30cd247d7c065026607f36f90f03` (origin/master @ 2026-11-04T21:03Z; re-verify with `git fetch origin` before continuing — PR #1813 was OPEN and active at start)
- **Phase**: baseline full build running (started ~21:10Z; -j2 dual resource_guard locks: `~/.cache/exp-rs-agent/host-build` → `build-r28-t04`; log `/tmp/r28t04-build.log`). NO production file may be edited until the baseline build finishes and baseline suites are recorded (see `BASELINE.md` build recipe).
- **Next command after build**: `ctest --test-dir build-r28-t04 -N` (inventory), then baseline runs of: `test_cartography_production_11`, `test_swarm_fix_agent_cartography`, `test_r25_d_cartography_dock|ux|e2e_display_cartography`, `test_layout_designer|_lifecycle`, `test_experiment_studio_dock`, `test_workflow_session_controller`, `test_spectral_profile_widget`, `test_orthorectification_dialog`, `test_r27_t04_*`, `test_workbench_full_shell_lifecycle`, `test_theme_selector_parity` — all with `QT_QPA_PLATFORM=offscreen`, `-j1`, under the build-dir lock. Save logs to `.planning/r28-t04/baseline-logs/`.

## Verified work queue (designs in DECISIONS.md; test drafts in `drafts/`)
1. **WP10 atlas**: reproduce the `#1812`-admitted atlas delivery failures on this build → root-cause → fix (no expected-file relaxing).
2. **F7**: `orthorectification_dialog.cpp` `rpcOrGcp` → RsScanPool async (generation + QPointer). Test draft `test_orthorectification_dialog_f7_append.cpp`.
3. **F12**: `qgslayoutdesignerdialog.cpp` `onLoadFromTemplate` → extract `loadTemplateFromFile()` + QgsPrintLayout-clone probe. Test draft `test_r28_t04_layout_template_safety.cpp`.
4. **F14**: `cartography_dock.cpp` taskId capture + `reapOrphanedTask()` watchdog (public static `taskFinishedOrGone(taskId, tasks)`), make `submitOperatorJob` public for tests. Test draft `test_r28_t04_dock_lifetimes.cpp` (also F18).
5. **F18**: `experiment_studio_dock.cpp` QPointer-to-qApp delivery + detached supervisor on teardown. Same draft file.
6. **WP03**: new `src/app/dialogs/import_batch_dialog.{h,cpp}` over `ImportSession` (+ wiring in `main_window_layers.cpp` addRaster/addVector). Test draft `test_r28_t04_import_batch.cpp`.
7. **WP04/05** (survey-verified): nodeSelected emission + selectStep session bootstrap; runUpToNode in-flight guard order; capture session/step at submit (`m_pendingSessionId/StepId`); `overlayPanelParams` static + persist panel form values before full runs; staleness invalidation on param edit; bind guards; save uses lastLoadedDefinition base. Test draft `test_r28_t04_workflow_panel_consistency.cpp` (extend with calibrated mid-flight cases for capture/guard-order).
8. **WP07**: map-tool QActionGroup + mapToolSet sync (+ registry action push via `CommandRegistry::action(id)`); identify gated on layers-with-reason; Select out of edit gate; edit-aware prompt on removing a modified layer; reset tool to pan after project open.
9. **WP11**: ALREADY_DONE_WITH_EVIDENCE (#1747 pattern verified at 3 write sites); optional B3 (.qgs suffix default) if hunk-local.

## Failed/blocked notes
- None yet. Exit 75 from resource_guard = busy, retry later; never delete live locks.

## Ledger
| 轮 | WP | 假设 | 修改/commit | 命令 | 实际结果 | 下一步 |
|---|---|---|---|---|---|---|
| 1 | 全 | #1812 遗留 atlas 失败可在本地复现 | — | (build running) | pending | baseline ctest |

## Update 2026-11-05 ~12:30Z
- WP10 CLOSED: cp11 33/33 green (baseline 3 red). Commits f0553bf255..ea676eef2e (9 commits).
- All production fixes implemented & committed: WP10 atlas ×4, F7, F12, F14, F18, WP03 dialog+wiring, WP04/05 controller+canvas+panel+editor, WP07 A1-A4, tests registered (5 new suites + F7 cases appended).
- Baselines recorded in .planning/r28-t04/baseline-logs/ (all family suites GREEN on pristine build except cp11 3 red — meaning any post-change failure is mine).
- H1 handoff written (TaskCenter worker-thread QGIS render → transparent PNG; T02/T03).
- PENDING: full targeted build queued (task exec_1b45a1f2; waits behind T05's full build, host lock) → then: run new suites + family regression ×2 → R1 review agent → fix → R2 → merge master → PR.
- F18 RED evidence: baseline test_experiment_studio_dock green pre-change (old dtor join path); the new teardown case asserts <1.5s against unfixed code = behavior-RED by construction (old code blocks for op duration). Compile-RED suites (F12/F14/WP03/workflow) — see commits; baseline logs prove the defects in source (file:line in DECISIONS/ISSUE_TRIAGE).
