# Track R13-L: Independent Code Review Report

**Date**: 2026-10-15
**Review Target**: `git diff origin/master...HEAD` on branch `hardening/r13-l-workbench-ui-durability`
**Review Scope**: Direction D (Workbench, UI, Shell, Workflow & Application Layer)
**Base Commit**: `f72f0c11731361488a384a085797aae64efaf7ee` (`origin/master`)
**Verdict**: **PASS** (Zero P0/P1 defects remaining)

---

## 1. Executive Summary

This independent review evaluated the changes made in Track R13-L across four critical focus areas:
1. Elimination of unmanaged detached worker reaper threads (`std::thread(...).detach()`) in `AgentOpsControlCenterDock`, `TeachingAdminDock`, and `ExperimentStudioDock` (#1493, DEF-PR1501-01).
2. UI responsiveness and memory exhaustion prevention via bounded matrix table previews (`kMaxMatrixDisplayRows = 500`) in `ExperimentStudioDock` (#1492, DEF-COR-02).
3. POSIX permission and symlink target preservation, alongside Windows atomic replacement (`ReplaceFileW` / `MoveFileExW`) in `PipelineEditorDock` (#1487).
4. Feature deletion verification and honest feedback in `QgisDesktopWindow` vector editing (#1453).

All changes strictly adhere to project conventions (`CMAKE_CXX_STANDARD 20`, Qt 6), maintain surgical ownership within `src/app/**` and `tests/**`, and are verified through deterministic Catch2 test suites.

---

## 2. Review Axes & Detailed Findings

### Axis 1: Concurrency, Mutexes & Thread Safety
- **`src/app/agent_ops/agent_ops_control_center_dock.{h,cpp}`**:
  - Replaced unmanaged `std::thread(...).detach()` when stopping/restarting worker threads with structured tracking:
    ```cpp
    struct RetiredWorker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex m_reaperMutex;
    std::vector<RetiredWorker> m_retiredWorkers;
    ```
  - `pruneReaperThreadsLocked()` safely inspects `done->load(std::memory_order_acquire)` and joins only completed threads, freeing resources without blocking the GUI event loop.
  - In `joinWorker()` and `~AgentOpsControlCenterDock()`, the cancel flag is signalled first (`m_workerCancel.store(true)`), followed by joining the active thread `m_workerThread` and unconditionally joining all remaining `m_retiredWorkers`.
  - Mutex locking granularity is minimal and never held across external callbacks or Qt signals.
- **`src/app/teaching_admin/teaching_admin_dock.{h,cpp}`**:
  - Replaced `.detach()` in `finishBatchOnUiThread` and `failBatchOnUiThread` with equivalent `RetiredThread` tracking guarded by `m_retiredMutex`.
  - Destructor signals cancellation and joins active `m_batchThread` and all retired threads, eliminating use-after-free risks on dock teardown.
- **`src/app/experiment_studio/experiment_studio_dock.{h,cpp}`**:
  - Replaced `.detach()` in `applyLiveStudyResult` with `RetiredStudyThread` tracking.
  - Destructor signals `m_studyCancel` and safely drains `m_studyThread` and `m_retiredStudyThreads`.

### Axis 2: Memory Lifecycle & Lifetimes
- Shared retirement flags (`std::shared_ptr<std::atomic<bool>> done`) guarantee safe lifetime even if the reaper lambda finishes before or after the dock checks completion.
- Qt objects (`QTableWidgetItem`, `QLabel`) in `rebuildMatrixTable()` are strictly created and owned within Qt's parent-child ownership model or table widget management.
- Bounding matrix display to `kMaxMatrixDisplayRows = 500` caps heap allocation from `O(N)` unbounded growth (where N could be 10,000+ combinations) to a deterministic upper bound of 500 rows.

### Axis 3: File Durability & Filesystem Semantics
- **`src/app/workflow/pipeline_editor_dock.cpp`**:
  - Symlink target resolution: `const QString targetPath = fileInfo.isSymLink() ? fileInfo.symLinkTarget() : filePath;` ensures that saving through a symlink modifies the target file rather than severing the link.
  - POSIX permissions: captures `origPermissions = targetInfo.permissions()` when `fileExisted` is true and restores them onto the staging temporary file via `tempFile.setPermissions(origPermissions)` before atomic replacement.
  - POSIX directory fsync: directory file descriptor opened and synced (`::fsync(dirFd)`) to guarantee inode metadata persistence.
  - Windows atomic replacement: utilizes `::ReplaceFileW(..., REPLACEFILE_WRITE_THROUGH | REPLACEFILE_IGNORE_MERGE_ERRORS, ...)` with fallback to `::MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` to provide write-through durability and atomic semantics on NTFS/ReFS without clobbering file streams.

### Axis 4: Error Paths & Fail-Closed Integrity
- **`src/app/main_window_vector.cpp`**:
  - In `cutFeatures()`: validates `vl->deleteSelectedFeatures(&deletedCount)`. If deletion fails or returns 0 deleted features, calls `vl->destroyEditCommand()`, presents an informative status message, and aborts before declaring success.
  - In `deleteSelectedFeatures()`: verifies `vlayer->deleteSelectedFeatures(&deletedCount)` and checks `success && deletedCount > 0`, avoiding misleading feedback when feature deletion is rejected by the underlying provider.

### Axis 5: Module Ownership & Scope
- Modified production files are strictly contained within `src/app/**`:
  - `src/app/agent_ops/agent_ops_control_center_dock.{h,cpp}`
  - `src/app/teaching_admin/teaching_admin_dock.{h,cpp}`
  - `src/app/experiment_studio/experiment_studio_dock.{h,cpp}`
  - `src/app/workflow/pipeline_editor_dock.cpp`
  - `src/app/main_window_vector.cpp`
- Modified test files are strictly confined to `tests/**`:
  - `tests/test_agent_ops_ui_smoke.cpp`
  - `tests/test_experiment_studio_dock.cpp`
  - `tests/test_workflow_pipeline_ui.cpp`
- **Zero overlap** with any other open PR (#1504, #1505, #1506, #1507, #1508, #1509).

### Axis 6: Test Validity & Oracles
- `tests/test_agent_ops_ui_smoke.cpp`: Rapid destruction test spins up 3 workers in quick succession (forcing 2 into retirement lists) and resets the dock within 10 iterations. Confirms clean shutdown without hangs or memory corruption.
- `tests/test_experiment_studio_dock.cpp`: Synthetic matrix test with 800 parameter combinations asserts that table row count is strictly capped at 500 and status label displays truncation disclosure.
- `tests/test_workflow_pipeline_ui.cpp`: Symlink test creates a symlink to a file with custom POSIX permissions, updates the file via the symlink path, and verifies that the symlink remains valid, points to the target, the target receives the updated JSON payload, and custom permissions are preserved.

---

## 3. Concluding Verdict

**PASS**: All verification gates passed cleanly. No regressions, no leaks, and no out-of-scope modifications. Ready for PR creation.
