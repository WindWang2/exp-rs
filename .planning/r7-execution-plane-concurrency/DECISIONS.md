# Architectural Decision Records: exp-rs R7 Execution Plane

**Date**: 2026-10-01  
**Milestone**: M0  
**Context**: Architectural Decisions for Concurrency, Lifetime, and Admission Closure  

---

## ADR-1: Strongly Typed Admission Refusal (`TaskAdmissionResult`)

### Context
`TaskCenter::submitJob`, `enqueueTask`, and `submitPipeline` currently return primitive `long`, where positive numbers are task IDs and `-1` represents all failure modes (queue full, memory exceeded, pipeline rejected, invalid request). Callers such as `RsJobRunner` and `GuiJobAdapter` check `if (taskId <= 0)` and silently discard failure callbacks (`m_onFailure = nullptr`), leaving client UIs (`QgsClassificationMainWindow`) permanently frozen in "Classifying...". Furthermore, `WorkflowRunCoordinator` treats `-1` as a fatal pipeline error ("no dispatchable steps") and writes a misleading `Failed` checkpoint to disk.

### Decision
1. Define a strongly typed `TaskAdmissionResult` struct:
   ```cpp
   struct TaskAdmissionResult {
       long taskId = -1;
       bool admitted = false;
       TaskAdmissionStatus status = TaskAdmissionStatus::Admitted;
       QString refusalReason;
       int currentQueueDepth = 0;
   };
   ```
2. For backward compatibility, `submitJob` can return positive task ID on success or `-1` while setting an out-parameter / status code, or provide an overload `submitJobWithAdmissionResult`.
3. Update `RsJobRunner`, `GuiJobAdapter`, and `QgsClassificationMainWindow` to explicitly handle refusals by executing refusal handlers and clearing busy state flags.
4. Update `WorkflowRunCoordinator::startTrackedPipeline` to recognize queue backpressure and report backpressure status without writing a false `Failed` checkpoint.

### Consequences
- Callers have full fidelity into why a task was not admitted.
- Zero silent drops of UI completion callbacks.
- Clear contract between transient backpressure and fatal task failures.

---

## ADR-2: In-Flight Persist Tracking and RAII Coordinator Drain

### Context
`WorkflowRunCoordinator::persistRun` is invoked asynchronously on `JobEngine` worker threads via `Qt::DirectConnection` from `TaskCenter::taskUpdated`. When a workflow completes, the run state transitions to `Completed` inside `m_mutex`, while `persistRun` executes outside `m_mutex` serialized by `m_checkpointIoMutex`. In unit tests and single-process ctest runs (`#1414`), the test thread sees `Completed`, exits the test, and destroys fixture directories or the process global singleton `s_instance`. The background worker thread subsequently accesses `m_latestPersistSeq.find(request.runId)` on deallocated memory, triggering an ASan `heap-use-after-free`.

### Decision
1. Introduce atomic in-flight persist tracking:
   ```cpp
   std::atomic<int> m_inFlightPersists{0};
   mutable std::condition_variable m_persistCv;
   ```
2. Protect every `persistRun` execution with an RAII guard that increments `m_inFlightPersists` on entry and decrements on exit, notifying `m_persistCv`.
3. Provide `drainPersists()` which waits until `m_inFlightPersists == 0` and flushes `m_checkpointIoMutex`.
4. Provide `shutdownForTests()` in `WorkflowRunCoordinator` that disconnects from `TaskCenter::taskUpdated`, calls `drainPersists()`, drains pending notifications, and clears all run and sequence maps.
5. Harden `CoordinatorFixture9` and `CoordinatorFixture` with RAII destructors calling `shutdownForTests()`.

### Consequences
- Complete elimination of the ASan heap-use-after-free.
- Eliminates cross-test pipeline ID collision and state pollution.
- Zero artificial `sleep()` oracles required.

---

## ADR-3: Cancellation TOCTOU Closure via `Cancelling` State Check

### Context
In `TaskCenter::flushPendingLaunches`, `m_mutex` is unlocked between staging the task and invoking `JobEngine::submitWithId`. If a cancellation request arrives during this unlocked window, `cascadeCancelTargetsLocked` sets `task.status = TaskStatus::Cancelling`. When `submitWithId` finishes and `m_mutex` is re-acquired, line 2948 currently checks only:
```cpp
if (m_tasks[launch.taskId].status == TaskStatus::Canceled)
```
Because the status is `Cancelling` (not yet `Canceled`), this check evaluates to false! The newly submitted job is treated as active (`mapped = true`), and `JobEngine::cancel` is never called, leaking an orphaned running worker in `JobEngine` (Race C).

### Decision
Update the re-lock check in `flushPendingLaunches`:
```cpp
if (m_tasks[launch.taskId].status == TaskStatus::Canceled ||
    m_tasks[launch.taskId].status == TaskStatus::Cancelling)
{
    m_taskByJobId.remove(submittedId);
    jobToCancel = submittedId;
}
```
If matched, immediately invoke `JobEngine::cancel(jobToCancel)` outside `m_mutex`.

### Consequences
- Prevents zombie workers from leaking when cancellation races admission and dispatch.
- Closes Race C deterministically.

---

## ADR-4: Inverted Retry Dispatch Ordering in `markTaskFailed`

### Context
In `TaskCenter::markTaskFailed`, auto-retry attempts currently call `flushPendingLaunches()` before `dispatchPendingCancels()`. When concurrency slots are saturated (`globalLimit = 1`), the retried task cannot be launched because the failed job has not yet been cancelled/cleared in `JobEngine`, causing slot starvation and deadlock (Race E).

### Decision
Reorder the cleanup and dispatch sequence in `markTaskFailed`:
```cpp
// 1. Dispatch pending engine cancellations to free slots immediately
dispatchPendingCancels(handlesToCancel, jobCancelTargets, ...);

// 2. Stage and flush launches for the retried task
flushPendingLaunches();
flushPendingSignals();
```

### Consequences
- Freed execution slots are immediately available for retry attempts.
- Closes Race E under saturated capacity.

---

## ADR-5: Strict Compilation & Test Resource Discipline (`-j2`)

### Context
High-concurrency compilation (`-j$(nproc)` or `-j4`) on multi-agent environments leads to memory exhaustion (OOM), compiler thrashing, and high latency.

### Decision
Strictly enforce:
```bash
export CMAKE_BUILD_PARALLEL_LEVEL=2
export CTEST_PARALLEL_LEVEL=2
```
All build invocations use `cmake --build <dir> -j2`. Sanitizer runs use `-j1` or `-j2`.

### Consequences
- Predictable, bounded CPU and memory footprint across all agent runs.
- Prevents process kills and hardware thrashing.
