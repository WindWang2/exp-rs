# Track R13-L: Baseline & Defect Census

**Date**: 2026-10-15
**Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r13-l-workbench-ui`
**Base Commit**: `f72f0c11731361488a384a085797aae64efaf7ee` (`origin/master`)
**Branch**: `hardening/r13-l-workbench-ui-durability`
**Domain**: Direction D (Workbench, UI, Shell, Workflow & Application Layer)

---

## 1. Context & Motivation

Track R13-L focuses on hardening the application layer, workbench docks, GUI concurrency boundaries, and workflow file durability, resolving open issues and forensic architecture review findings:

1. **Issue #1493 & DEF-PR1501-01** [P1 Defect]:
   - Unsafe `.detach()` worker thread reaping across three core docks (`AgentOpsControlCenterDock`, `TeachingAdminDock`, and `ExperimentStudioDock`).
   - Moving previous workers into detached threads leaves them running unjoined in the background, racing with new workers upon rapid restart/cancel and leaking running threads dereferencing freed `this` pointers during dock destruction.
2. **Issue #1492 & DEF-COR-02** [P1 Defect]:
   - Real-time study execution thread lifecycle safety and heap blowup prevention in `ExperimentStudioDock`.
   - Bounded matrix display rows (`kMaxMatrixDisplayRows = 500`) to avoid synchronous heap exhaustion when previewing large parameter studies.
3. **Issue #1487** [P1 Defect]:
   - Pipeline definition save durability and atomic replacement in `src/app/workflow/pipeline_editor_dock.cpp`.
   - Preserving existing POSIX file permissions (preventing regression to 0600 on overwrite).
   - Resolving symlinks before saving to avoid clobbering symlink targets.
   - On Windows: using atomic replacement (`ReplaceFileW`) instead of non-atomic `.bak` copy/rename.
4. **Issue #1453** [P2 Defect]:
   - Honest cut feedback in vector editing (`src/app/main_window_vector.cpp`).
   - Checking feature deletion results, rolling back edit command on failure, and avoiding misleading success feedback.

---

## 2. Active PR & Worktree Non-Interference Matrix

- Open PR #1504: `src/data/*`, `src/dataset/*`, `src/geospatial/*`, `tests/test_io_*` (No overlap)
- Open PR #1505: `src/core/temporal_cube.cpp`, `src/geospatial/*`, `src/python/*`, `src/runtime/gpu/*`, `src/science_context/*` (No overlap)
- Open PR #1506: `src/analysis/*`, `src/operators/rs/*`, `src/processing/*` (No overlap)
- Open PR #1507: `src/agent/*`, `src/agent_ops/operations_coordinator.cpp`, `src/plugins/*`, `src/sdk/*`, `src/workflow/pipeline_run_coordinator.*` (No overlap)
- Open PR #1508: `src/operators/runtime/*` (No overlap)
- Open PR #1509: `src/agent_ops/operations_coordinator.cpp`, `src/science_context/*`, `src/verification/*` (No overlap)
- Track R13-L modified files are strictly confined to `src/app/**` and corresponding dedicated tests in `tests/**`.
