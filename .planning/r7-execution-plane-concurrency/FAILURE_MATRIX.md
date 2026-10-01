# Failure Modes & Root-Cause Matrix: exp-rs R7 Execution Plane

**Date**: 2026-10-01  
**Milestone**: M0  
**Context**: Comprehensive Concurrency & Lifetime Defect Classification  

---

## 1. Concurrency Defect & Failure Mode Matrix

| ID | Title & Severity | Race / Mode | Affected Source Files | Root Cause | Failure Impact | Milestone & Track |
|---|---|---|---|---|---|---|
| **FM-1** | **ASan Heap-Use-After-Free in Coordinator Persist**<br>`[P0 Critical]` | Lifetime Inversion | `src/workflow/workflow_run_coordinator.cpp:424`<br>`tests/test_execution_plane_9.cpp:782-803` | `persistRun` executes asynchronously on `JobEngine` worker thread after test thread exits and fixture/process tears down `s_instance`. | ASan crash: invalid read in `std::map<std::string, quint64>::find` during test exit or between tests. | M1<br>(Agent B) |
| **FM-2** | **Cancellation TOCTOU Leaking Zombie Workers**<br>`[P0 Critical]` | Race C (Cancel vs Submit) | `src/processing/framework/task_center.cpp:2948`<br>`src/jobs/job_engine.cpp:1006` | In `flushPendingLaunches`, re-lock check tests only `status == Canceled`, missing `TaskStatus::Cancelling` set during unlocked window. | Orphaned worker process/thread runs indefinitely in `JobEngine`, leaking CPU, memory, and slots. | M4<br>(Agent C) |
| **FM-3** | **Inverted Auto-Retry Slot Starvation Deadlock**<br>`[P0 Critical]` | Race E (Release vs Retry) | `src/processing/framework/task_center.cpp:3587-3596` | `markTaskFailed` calls `flushPendingLaunches()` before `dispatchPendingCancels()`, staging retry before freeing dead engine job. | Auto-retry blocks on exhausted concurrency slots; pipeline hangs indefinitely under tight resource limits. | M4<br>(Agent C) |
| **FM-4** | **Untyped Admission Refusal & UI Permanent Freeze**<br>`[P1 Major]` | Error Semantic Conflation | `src/processing/framework/task_center.cpp:1605`<br>`src/app/shell/rs_job_runner.cpp:40`<br>`src/app/shell/gui_job_adapter.cpp:43`<br>`src/app/classification/qgsclassificationmainwindow.cpp:2540` | `submitJob` returns primitive `-1` on queue saturation. Callers drop `onFinished` / `m_onFailure` without calling them. | UI state `setClassifyBusy(true)` is never cleared; classification window stays permanently locked in "Classifying...". | M3<br>(Agent A) |
| **FM-5** | **False `Failed` Checkpoint on Pipeline Refusal**<br>`[P1 Major]` | Semantic Drift | `src/workflow/workflow_run_coordinator.cpp:538` | When `submitPipeline` returns `-1`, coordinator writes a `Failed` checkpoint attributing error to "no dispatchable steps". | Durable run history is corrupted with misleading failure diagnostics for a task that was simply refused. | M3<br>(Agent A) |
| **FM-6** | **Interactive Job Starvation under Batch Load**<br>`[P1 Major]` | Race F (Batch vs Interactive) | `src/processing/framework/task_center.h:784-793`<br>`src/processing/framework/task_center.cpp:2300-2450` | `ReadyEntryGreater` heap comparator lacks `LatencyClass` weighting. Interactive tasks are sorted behind thousands of batch jobs. | Interactive queries (UI preview, click inspectors) freeze for minutes behind large batch queues. | M3<br>(Agent A) |
| **FM-7** | **Cross-Test Pipeline ID Collision & Stale State**<br>`[P1 Major]` | Teardown Leakage | `src/workflow/workflow_run_coordinator.h:241`<br>`src/workflow/workflow_run_coordinator.cpp:520` | `TaskCenter::shutdownForTests()` resets `m_nextPipelineId` to 1, but `WorkflowRunCoordinator` retains old runs in `m_runsByPipeline`. | Subsequent tests fold new tasks into stale runs from prior tests, persisting checkpoints into deleted directories. | M1<br>(Agent B) |
| **FM-8** | **Quadratic Aging Sweep Hot-Path Bottleneck**<br>`[P2 Moderate]` | Scalability Degradation | `src/processing/framework/task_center.cpp:2364-2374` | `applyAgingSweepLocked` collects `m_readySerial.keys()` and scans all queued tasks on every admission pass ($O(N^2 \log N)$). | Severe CPU spikes and allocation churn during 10k rapid short-job submission bursts. | M3<br>(Agent A) |
| **FM-9** | **Fused Execution Chain Cancellation Parity Gap**<br>`[P1 Major]` | State Machine Parity | `src/processing/framework/fused_chain.cpp:180-220` | Cancelling a fused chain fails to abort downstream intermediate sub-steps or release intermediate resource allocations. | Inconsistent state and orphaned resource accounting compared to standard sequential task pipelines. | M4<br>(Agent C) |
| **FM-10** | **Worker Process Crash Resource Leak**<br>`[P1 Major]` | Failure Recovery | `src/processing/framework/worker_process_guard.cpp:90-140` | Abnormal worker process termination does not trigger terminal reconciliation in `TaskResourceBudget2`. | Budget slots remain locked; subsequent tasks are rejected due to phantom budget exhaustion. | M4<br>(Agent C) |

---

## 2. Deterministic Verification & Validation Plan

Every failure mode in this matrix must be covered by a deterministic verification oracle:

- **FM-1 (UAF)**: ASan compilation (`-DENABLE_SANITIZERS=ON`) + artificial delay in `persistRun` + `ctest -R "explain dumps"` $\to$ must pass with 0 errors across 50 runs.
- **FM-2 (Race C)**: Thread barrier synchronizing task cancellation at Point Alpha (between unlocked `submitWithId` and re-lock) $\to$ prove job is cancelled in `JobEngine` and no zombie worker survives.
- **FM-3 (Race E)**: Saturated single-worker budget (`globalLimit = 1`) + failing task auto-retry $\to$ prove retry starts immediately without blocking on dead job.
- **FM-4 (UI Freeze)**: Saturated pending queue $\to$ call `RsJobRunner::submitJob` $\to$ prove `onFinished` callback is invoked with typed error status and UI is unblocked.
- **FM-5 (False Checkpoint)**: Saturated pending queue $\to$ `startTrackedPipeline` $\to$ verify run state transitions to `Refused` or returns error before writing false `Failed` checkpoint.
- **FM-6 (Race F)**: 1,000 batch tasks queued $\to$ submit 1 interactive task $\to$ prove interactive task is admitted in the very next scheduling pass.
- **FM-8 (Aging Scalability)**: Submit 10k mock tasks $\to$ total admission and scheduling time $\le 5$ seconds; no $O(N^2)$ key-list allocations.
