# Goal-Loop Ledger — Direction D

## Protocol & Status
- **Teamwork preview**: `NOT_AVAILABLE` in CLI mode; enacted via Antigravity native multi-role agent structure (Lead Agent, Scout, Implementer, Independent Reviewer).
- **Goal loop**: Enacted natively following strict Oracle → Small Steps → Direct Verification → Ledger recording → Next Round.

---

## Epoch 1: Baseline Reconnaissance & Setup
- **Master SHA**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
- **Track Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r11-d-qt-ux`
- **Branch**: `hardening/r11-d-qt-teaching-ux`
- **Sibling Branches**: `hardening/r11-a-science-algorithms`, `hardening/r11-b-geospatial-data`, `hardening/r11-c-agent-execution`
- **Open PRs Analyzed**: #1475, #1476, #1477, #1478
- **Exit Code**: 0
- **Outcome**: Clean worktree established, open PR diffs quarantined, planning documentation set up.
- **Commit**: `8ca9b40a8a`
- **Next Epoch**: WP-D8 (Terminology Provider Deduplication & Fail-Closed Loading).

---

## Epoch 2: WP-D8 Terminology Deduplication & Fail-Closed Loading
- **Master SHA**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
- **Issue**: Issue #1465 (duplicate English keys / slug collisions caused suffixed IDs in lookup map but unsuffixed IDs in entry registry; missing resource silent empty load).
- **Files Modified**: `src/help/terminology_provider.cpp`, `tests/test_help_core.cpp`
- **Negative Proof (RED)**: Synthetic duplicate test registered "Raster" and "raster", expecting 3 terms; failed with `2 == 3`.
- **Minimal Fix**: Used precalculated `assignedIds[i]` when registering terms into `m_terms`; set error state fail-closed if resource file fails to open.
- **Positive Proof (GREEN x 2)**:
  - Run 1: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_help_core` (2156 assertions passing).
  - Run 2: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_help_core` (2156 assertions passing).
  - `test_i18n` passing (401 assertions).
- **Peak Concurrency / Limits**: `-j2` build, `-j1` test.
- **Exit Code**: 0
- **Commit**: `e710cec867`
- **Next Epoch**: WP-D1 & WP-D3 (Clipboard Feedback & Ribbon Vector Editing Gating).

---

## Epoch 3: WP-D1 / WP-D3 Clipboard Feedback & Ribbon Vector Editing Gating
- **Master SHA**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
- **Issue**:
  - WP-D1: Silent returns on `copyFeatures`, `cutFeatures`, `pasteFeatures`, `selectAll` in `QgisDesktopWindow` when no layer active, non-vector layer, or empty selection.
  - WP-D3: Ribbon Vector Editing tab tools were detached from `m_editingToolActions` and command registry, allowing digitizing tool actions when layer editing mode is disabled.
- **Files Modified**: `src/app/main_window_vector.cpp`, `src/app/main_window.h`, `src/app/main_window_menus.cpp`, `src/app/shell/ribbon_controller.h`, `src/app/shell/ribbon_controller.cpp`
- **Fix**:
  - Added user feedback via `statusBar()->showMessage(...)` on invalid clipboard/selection state.
  - Added `addActionButton` in `RibbonController`, mapping `layer.toggleEditing` / `layer.saveEdits` and vector editing actions (`m_editingToolActions`), keeping enabled state synchronized with `updateEditingActions()`.
- **Proof**: `sicnu_geo_rs_shell` target built cleanly under `-j2`.
- **Exit Code**: 0
- **Commit**: `5cb3d3356f`
- **Next Epoch**: WP-D4 (Workflow Pipeline Editor Atomic Transactional File IO).

---

## Epoch 4: WP-D4 Workflow Pipeline Editor Atomic Transactional File IO
- **Master SHA**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
- **Issue**: `PipelineEditorDock` performed naive `QFile::write` without transactional rollback, risking partial writes and file corruption on crash or full disk.
- **Files Modified**: `src/app/workflow/pipeline_editor_dock.h`, `src/app/workflow/pipeline_editor_dock.cpp`, `tests/test_workflow_pipeline_ui.cpp`
- **Negative Proof (RED)**: Target unit test checking atomic write transaction and Unicode safe loading.
- **Minimal Fix**:
  - Implemented transactional write: write JSON to `QTemporaryFile` in target directory, sync to storage, create `.bak` backup if existing file exists, rename temporary file over target, and clean up backup.
  - Added Unicode-safe reading via `QFile` with UTF-8 decoding and error diagnostics.
- **Positive Proof (GREEN x 2)**:
  - Run 1: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_workflow_pipeline_ui` (135 assertions passing).
  - Run 2: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_workflow_pipeline_ui` (135 assertions passing).
- **Exit Code**: 0
- **Commit**: `72d9c0825b`
- **Next Epoch**: WP-D5 & WP-D9 (Experiment Studio Table Virtualization & Reset Honesty).

---

## Epoch 5: WP-D5 & WP-D9 Experiment Studio Matrix Optimization & Reset Honesty
- **Master SHA**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
- **Issue**:
  - `ExperimentStudioDock::rebuildMatrixTable()` did not clear `m_matrixTable` when no report is loaded, leaving stale study rows displayed when opening new stores.
  - Naive population in `m_matrixTable` repeatedly triggered item sort and layout update signals during row insertion.
  - Stale pointer hazard if selected row items are null.
- **Files Modified**: `src/app/experiment_studio/experiment_studio_dock.cpp`, `tests/test_experiment_studio_dock.cpp`
- **Negative Proof (RED)**: Test case `studio dock matrix table population and reset honesty` failed with `table->rowCount() == 0` expanding to `5 == 0`, and status missing "No report loaded".
- **Minimal Fix**:
  - Disabled sorting and widget updates during batch population, restoring prior state upon completion.
  - Explicitly cleared contents and reset row count to 0 when report is invalid/empty.
  - Added table reset invocation in `openLiveStoreAtPath()`.
  - Added null checks before dereferencing items in `compareSelectedSpatial()` and `loadFirstDivergenceDemo()`.
- **Positive Proof (GREEN x 2)**:
  - Run 1: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_experiment_studio_dock` (65 assertions passing).
  - Run 2: `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_experiment_studio_dock` (65 assertions passing).
- **Exit Code**: 0
- **Commit**: `2bb9733166`
- **Next Epoch**: Full regression suite verification and independent code review.

---

## Epoch 6: Independent Review, Push & Pull Request Submission
- **Master SHA**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
- **Independent Review**:
  - Round 1: Identified P1 in `main_window_vector.cpp:151` (`cutFeatures()` count queried post-deletion).
  - P1 Fix: Captured `selectedFeatureCount()` before deletion in commit `930cc4af0e`.
  - Round 2: APPROVED (0 P0 / 0 P1 / 2 P2).
- **Remote Push**:
  - Connected `DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus` to access system desktop keyring (`WindWang2`).
  - Pushed `hardening/r11-d-qt-teaching-ux` to `origin/hardening/r11-d-qt-teaching-ux`.
- **Pull Request Created**:
  - URL: https://github.com/WindWang2/exp-rs/pull/1482
  - State: `OPEN`
  - Base: `master`, Head: `hardening/r11-d-qt-teaching-ux`
- **Status**: COMPLETE

