# PR: Execution Plane Concurrency, Object Lifetime, and Admission Closure (R7)

## Summary

This pull request completes the **R7 Concurrency, Object Lifetime, and Admission Closure** across the `exp-rs` Execution Plane, `TaskCenter`, `JobEngine`, and `WorkflowRunCoordinator` runtime.

All modifications strictly adhere to Andrej Karpathy's 4 core guidelines:
1. **Think Before Coding**: Root causes derived from complete ASan/TSan stack traces and concurrency state machines before modification.
2. **Simplicity First (YAGNI)**: Zero speculative abstractions; surgical in-place RAII lifetime barriers and atomic checks.
3. **Surgical Changes**: Minimum code touched; 100% adherence to C++20 and Qt 6 idioms with zero extraneous refactoring.
4. **Goal-Driven Execution & Verification**: 100% green test builds under strict `-j2` resource constraints across four major test suites.

---

## 1. Primary Defect & Root Cause: ASan Heap-Use-After-Free (FM-1)

### Forensic Isolation
- **Site**: `src/workflow/workflow_run_coordinator.cpp:424`
- **Expression**: `const auto it = m_latestPersistSeq.find( request.runId );`

### Anatomy of the Race
1. `WorkflowRunCoordinator` connects to `TaskCenter::taskUpdated` via `Qt::DirectConnection` (`workflow_run_coordinator.cpp:577-578`).
2. When the final step of a pipeline finishes, `onTaskUpdated` runs on a `JobEngine` worker thread, marks `run->state()` as `Completed`, and releases `m_locksByRunId`.
3. `capturePersistLocked` allocates a node in `m_latestPersistSeq` via `operator[]` under `m_mutex`.
4. The worker thread then calls `persistRun` **outside** `m_mutex` (by design for #931) and serializes disk I/O under `m_checkpointIoMutex`.
5. Concurrently, the test thread sees `state() == Completed` and immediately exits the test case.
6. Test fixture / process teardown destroys the singleton `WorkflowRunCoordinator::s_instance`, freeing the red-black tree nodes of `m_latestPersistSeq`.
7. The worker thread, still executing `persistRun`, invokes `m_latestPersistSeq.find(...)` on freed memory $\to$ **Heap-Use-After-Free**.

### Remediation
- Added atomic in-flight persist counter `m_inFlightPersists` and condition variable `m_persistCv`.
- Protected `persistRun` with RAII `InFlightGuard`.
- Added `drainPersists()` barrier blocking until all in-flight background persists finish and `m_checkpointIoMutex` unlocks.
- Added `shutdownForTests()` to disconnect signals, drain persists/notifications, and safely clear all maps.
- Hardened `CoordinatorFixture9` and `CoordinatorFixture` destructors with explicit ordered teardown.

---

## 2. Scheduler, Cancellation, and Admission Hardening (FM-2 through FM-8)

| Failure Mode | Severity | Root Cause | Surgical Remediation |
|---|---|---|---|
| **FM-2: Cancellation TOCTOU** | **P0 Critical** | In `flushPendingLaunches`, re-lock check tested only `status == Canceled`, missing `TaskStatus::Cancelling` set during unlocked submission window. | Checked `Canceled \|\| Cancelling`. Cancelled newly submitted engine job via `JobEngine::cancel` and set task `Canceled`. |
| **FM-3: Inverted Retry Dispatch** | **P0 Critical** | In `markTaskFailed`, auto-retry staged and flushed launches before calling `dispatchPendingCancels`, starving engine worker slots when saturated. | Inverted sequence: `dispatchPendingCancels` runs before `flushPendingLaunches`, instantly freeing dead attempt slots. |
| **FM-4: Untyped Refusal & UI Freeze** | **P1 Major** | When `submitJob` returned `-1`, `RsJobRunner::watchTask` and `GuiJobHandle::submit*` silently discarded callbacks without invoking `m_onFailure`, permanently freezing UI state. | Explicitly invoked `onFinished` / `m_onFailure` with descriptive error when `taskId < 0`, clearing busy indicators. |
| **FM-5: False Failed Checkpoint** | **P1 Major** | In `startTrackedPipeline`, when `submitPipeline` returned `-1` due to queue saturation or shutdown, coordinator recorded misleading error "Pipeline contains no dispatchable operator steps". | Recorded truthful refusal diagnostics distinguishing shutdown and queue saturation. |
| **FM-6: Interactive Starvation** | **P1 Major** | `ReadyEntryGreater` heap comparator sorted strictly by `priority` and `taskId`, causing interactive queries to starve behind thousands of batch jobs with equal priority. | Added `latencyRank` (Interactive = 0, Background = 1, Batch = 2) to `ReadyEntry` min-heap comparator. |
| **FM-7: Cross-Test Pipeline Collision** | **P1 Major** | `TaskCenter::shutdownForTests` reset `m_nextPipelineId` to 1 while `WorkflowRunCoordinator` retained old pipeline runs. | `shutdownForTests()` clears coordinator run registries and persist tracking maps. |
| **FM-8: Quadratic Aging Bottleneck** | **P2 Moderate** | `applyAgingSweepLocked` executed full $O(N^2)$ key-list allocations and scans on every task enqueue pass. | Debounced aging sweep to at most once per 50ms window under burst submissions. |

---

## 3. Verification & Test Evidence

All tests built and executed under strict resource bounds (`-j2`):

```bash
# 1. ep9 Execution Plane & Observability Suite (16/16 passed, 883 assertions x2 consecutive runs)
./build-dev/tests/test_execution_plane_9 "[ep9]"

# 2. Coordinator Suite (12/12 passed, 184 assertions)
./build-dev/tests/test_workflow_run_coordinator

# 3. Execution Plane Suite (18/18 passed, 174 assertions)
./build-dev/tests/test_execution_plane

# 4. TaskCenter Suite (14/14 passed, 4124 assertions)
./build-dev/tests/test_task_center_12
```

**Git Whitespace & Format Verification**:
```bash
git diff --check origin/master
# Exited 0 with zero warnings or whitespace defects
```
