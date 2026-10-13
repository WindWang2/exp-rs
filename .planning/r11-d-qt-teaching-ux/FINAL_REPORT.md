# Final Engineering Report — Direction D: Qt/QGIS Workbench, Interaction States, Teaching & Help

- **Repository**: `WindWang2/exp-rs`
- **Track**: Direction D
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r11-d-qt-ux`
- **Branch**: `hardening/r11-d-qt-teaching-ux`
- **Base Commit**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758` (`origin/master`)
- **Status**: **COMPLETE — Ready for PR**

---

## 1. Executive Summary

Direction D addresses stability, state honesty, data protection, and performance across the existing Qt 6 / QGIS desktop workbench, layer interactions, workflow UI, student teaching labs, and F1 context help systems.

All changes strictly follow Karpathy's 4 core guidelines:
1. **Think Before Coding**: Isolated exact root causes and quarantined open PR diffs (#1475, #1476, #1477, #1478).
2. **Simplicity First (YAGNI)**: No new UI frameworks or duplicate subsystems; purely surgical fixes to existing implementations.
3. **Surgical Changes**: Minimal, isolated line edits adhering to C++20 and Qt 6 conventions.
4. **Goal-Driven Execution & Verification**: Verifiable RED → GREEN x 2 test loops with Catch2, zero warnings on `-j2` compilation, and independent review with 0 P0 / 0 P1 findings.

---

## 2. Work Packages Completed

### WP-D8: Terminology Deduplication & Fail-Closed Loading (Issue #1465)
- **Problem**: In `TerminologyProvider::appendDescriptors()`, duplicate English terms / slug collisions led to suffixed IDs being assigned in `idByEnLower` but unsuffixed IDs used during descriptor registration in `m_terms`, causing collisions and missing related links. Missing embedded resources also failed open silently.
- **Solution**:
  - Maintained linear `assignedIds` vector pre-calculated during the dedup loop, assigning each entry its exact unique dedup-suffixed ID.
  - Fail-closed error reporting when glossary resource fails to open.
- **Verification**: Catch2 tests in `test_help_core.cpp` (2,156 assertions passing).

### WP-D1 & WP-D3: Vector Editing State Contracts & Ribbon Tool Gating
- **Problem**:
  - `QgisDesktopWindow::cutFeatures()`, `copyFeatures()`, `pasteFeatures()`, `selectAll()` silently failed without user feedback when no layer was active, layer was non-vector, or selection was empty.
  - Ribbon Vector Editing tab tools were disconnected from `m_editingToolActions`, allowing digitizing and editing tool invocation when vector editing was disabled.
- **Solution**:
  - Added user-visible `statusBar()->showMessage()` feedback across all clipboard and selection actions.
  - Fixed `cutFeatures()` to cache `selectedFeatureCount()` before deletion so the status bar truthfully reports the number of cut features instead of 0.
  - Added `RibbonController::addActionButton()` to map window editing actions directly to Ribbon tool buttons, dynamically synchronizing enabled states with vector layer editability via `updateEditingActions()`.
- **Verification**: `sicnu_geo_rs_shell` built cleanly with zero warnings under `-j2`.

### WP-D4: Workflow Pipeline Editor Atomic Transactional File IO
- **Problem**: `PipelineEditorDock` performed naive `std::ofstream` / `std::ifstream` writes and reads without transactional rollback, risking partial writes on crash or power loss, and Unicode encoding failures on Windows.
- **Solution**:
  - Implemented transactional atomic save (`writePipelineDefinitionFile`): writes JSON to `QTemporaryFile` in target directory, syncs to storage, creates `.bak` backup of existing file, atomically renames temporary file over target, and rolls back upon failure.
  - Implemented Unicode-safe reading (`readPipelineDefinitionFile`) using `QFile` with UTF-8 decoding.
- **Verification**: Catch2 test case `PipelineEditorDock: atomic pipeline save and read contracts` in `test_workflow_pipeline_ui.cpp` (135 assertions passing).

### WP-D5 & WP-D9: Experiment Studio Table Virtualization & Reset Honesty
- **Problem**:
  - Naive population in `m_matrixTable` repeatedly triggered item sort and layout update signals during row insertion.
  - Opening a new live store did not clear previously displayed demo matrix rows, causing stale display.
  - Stale pointer hazards when accessing `m_matrixTable->item(...)` in spatial and divergence analysis.
- **Solution**:
  - Gated widget sorting and updates (`setSortingEnabled(false)`, `setUpdatesEnabled(false)`) during batch population.
  - Reset row count to 0 and cleared contents when no report is loaded or when opening a fresh store in `openLiveStoreAtPath()`.
  - Added defensive null pointer checks before dereferencing table items.
- **Verification**: Catch2 test case `studio dock matrix table population and reset honesty` in `test_experiment_studio_dock.cpp` (65 assertions passing).

---

## 3. Automated Test Verification (Oracle Matrix)

| Suite Binary | Target Component | Assertions Passed | Exit Code | Red-Green-Green Verified |
| :--- | :--- | :--- | :--- | :--- |
| `test_help_core` | Help & Terminology Provider | 2,156 / 2,156 | 0 | Yes |
| `test_i18n` | Internationalization & Translation | 401 / 401 | 0 | Yes |
| `test_workflow_pipeline_ui` | Workflow Pipeline UI & IO | 135 / 135 | 0 | Yes |
| `test_experiment_studio_dock` | Experiment Studio Dock & Table | 65 / 65 | 0 | Yes |

---

## 4. Independent Review Sign-Off

- **Independent Reviewer**: Detached subagent (zero implementation involvement).
- **Round 1 Status**: Conditional Approval (1 P1 finding on `cutFeatures()` count).
- **Round 2 Status**: **APPROVED (0 P0 / 0 P1 / 2 P2)**.
  - P1 resolved in commit `930cc4af0e`.
  - All lifetime, threading, cross-platform, and transactional contracts verified.
