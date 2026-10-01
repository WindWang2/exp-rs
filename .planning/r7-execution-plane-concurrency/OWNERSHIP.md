# Work Packages & Agent Ownership: exp-rs R7 Execution Plane Concurrency & Admission Closure

**Date**: 2026-10-01  
**Milestone**: M0  
**Context**: exp-rs R7 Concurrency & Admission Hardening  

---

## 1. Specialist Agents & Track Breakdown

The project allocates three specialized coding tracks plus an independent challenger:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Project Orchestrator                            │
└───────┬──────────────────────────┬───────────────────────────┬─────────┘
        │                          │                           │
        ▼                          ▼                           ▼
┌───────────────┐          ┌───────────────┐           ┌───────────────┐
│    Agent A    │          │    Agent B    │           │    Agent C    │
│  Scheduler &  │          │  Telemetry &  │           │   Stress &    │
│   Admission   │          │   Lifetime    │           │ Determinism   │
└───────┬───────┘          └───────┬───────┘           └───────┬───────┘
        │                          │                           │
        └──────────────────────────┼───────────────────────────┘
                                   │
                                   ▼
                       ┌───────────────────────┐
                       │      Challenger &     │
                       │   Forensic Auditor    │
                       └───────────────────────┘
```

---

## 2. Track Ownership & Work Packages

### 2.1 Agent A: Scheduler, Admission, Fairness & Backpressure
- **Core Focus**: Task admission gatekeeping, strongly typed refusals, backpressure propagation, starvation avoidance, aging sweep performance.
- **Assigned Milestones**: Milestone M3 (Admission, Fairness & Backpressure), Milestone M4 (co-owner on cancellation semantics).
- **Assigned Work Packages**:
  - `WP-E`: Strongly Typed Admission Refusal (`TaskAdmissionResult` / `AdmissionError`).
  - `WP-F`: Elimination of UI freezes and silent drops in `RsJobRunner`, `GuiJobAdapter`, and `QgsClassificationMainWindow`.
  - `WP-G`: WorkflowRunCoordinator backpressure propagation on queue refusal (eliminating false `Failed` checkpoints).
  - `WP-J`: Interactive Starvation Avoidance (`LatencyClass` weighting in `ReadyEntryGreater` / `m_readyHeap`) and aging sweep optimization in `applyAgingSweepLocked`.
- **Owned File Boundaries**:
  - `src/processing/framework/task_center.h` (admission interfaces, ReadyEntry structs)
  - `src/processing/framework/task_center.cpp` (admission paths, `enqueueTask`, `applyAgingSweepLocked`, `processNextQueuedTasks`)
  - `src/processing/framework/task_resource_budget.h/.cpp`
  - `src/app/shell/rs_job_runner.h/.cpp`
  - `src/app/shell/gui_job_adapter.h/.cpp`
  - `src/app/classification/qgsclassificationmainwindow.cpp`
  - `tests/test_task_center.cpp`
  - `tests/test_task_center_12.cpp`

### 2.2 Agent B: Telemetry, Evidence, Lifetime & Teardown Closure
- **Core Focus**: ASan Heap-Use-After-Free root-cause fix, coordinator in-flight persistence tracking, explicit singleton teardown ordering, reader/writer snapshot semantics.
- **Assigned Milestones**: Milestone M1 (ASan Heap-UAF & Lifetime Closure), Milestone M2 (Concurrency Map & Lifetime Contracts).
- **Assigned Work Packages**:
  - `WP-B`: ASan Heap-Use-After-Free elimination in `m_latestPersistSeq.find` (`workflow_run_coordinator.cpp:424`).
  - `WP-A`: Formalization of reader/writer snapshot semantics (`diagnosticsSnapshot`, `explainDump`, `explainRun`, `counters`).
  - `WP-C`: Observer notification decoupling outside internal locks (`taskUpdated`, `runStateChanged`).
  - `WP-D`: Explicit singleton teardown ordering across `WorkflowRunCoordinator`, `TaskCenter`, `JobEngine`, and `ExecutionTelemetry`.
  - `WP-I`: Fixture RAII teardown hardening (`CoordinatorFixture9`, `CoordinatorFixture`) and cross-test state isolation.
- **Owned File Boundaries**:
  - `src/workflow/workflow_run_coordinator.h`
  - `src/workflow/workflow_run_coordinator.cpp`
  - `src/workflow/workflow_run.h/.cpp`
  - `src/runtime/observability/execution_telemetry.h`
  - `src/runtime/observability/execution_telemetry.cpp`
  - `tests/test_execution_plane_9.cpp`

### 2.3 Agent C: Stress, Deterministic Harness, Cancellation & Sanitizers
- **Core Focus**: Cancellation state machine closure, TOCTOU prevention (Race C), inverted retry dispatch ordering (Race E), deterministic concurrency test suite (Races A–F), 10k short-job drain scalability, sanitizer verification.
- **Assigned Milestones**: Milestone M4 (Cancellation State Machine & Parity), Milestone M5 (Deterministic Concurrency Harness, Scalability & Stress).
- **Assigned Work Packages**:
  - `WP-H`: Cancellation TOCTOU closure in `flushPendingLaunches` (checking `TaskStatus::Cancelling` to prevent zombie workers).
  - `WP-N`: Inverted retry dispatch ordering in `markTaskFailed` (`dispatchPendingCancels` before `flushPendingLaunches`).
  - `WP-O`: Cancellation and resource accounting parity for fused execution chains.
  - `WP-P`: Worker crash cleanup and execution plane resource reclamation (`WorkerProcessGuard`).
  - `WP-K`: Deterministic concurrency test harness for Races A through F using synchronization primitives.
  - `WP-L`: 10,000 rapid short-job throughput and memory scalability verification.
  - `WP-Q`: Full ASan & UBSan test verification strictly under `-j2`.
- **Owned File Boundaries**:
  - `src/processing/framework/fused_chain.h/.cpp`
  - `src/processing/framework/worker_process_guard.h/.cpp`
  - `src/processing/framework/worker_execution_route.h/.cpp`
  - `src/jobs/job_engine.h/.cpp` (cancellation interaction hooks)
  - `tests/test_execution_plane_7.cpp`
  - `tests/test_execution_plane_8.cpp`
  - `tests/test_concurrency_stress.cpp`

### 2.4 Independent Challenger & Forensic Auditor
- **Core Focus**: Adversarial review, failure matrix verification, forensic code audit, zero-hallucination validation.
- **Assigned Milestones**: Milestone M6 (Independent Challenger/Auditor Review & Final Delivery).
- **Responsibilities**:
  - Verify that no hardcoded strings, dummy facades, or fake passes exist.
  - Actively attempt to reproduce Races A through F and UAF under high thread contention.
  - Verify clean git diff hygiene, adherence to Karpathy's 4 core guidelines, and 100% green test builds.

---

## 3. Conflict Prevention & Coordination Rules

1. **Sequential Handoff on Shared Files**:
   - `src/processing/framework/task_center.cpp` touches both Agent A (admission/queue logic) and Agent C (cancellation/retry dispatch).
   - Agent A modifies admission paths in M3. Agent C modifies cancellation/retry dispatch in M4.
2. **Interface Contract Pre-Agreement**:
   - Typed admission refusal struct (`TaskAdmissionResult`) defined in M3 is consumed by `WorkflowRunCoordinator` (Agent B) and `RsJobRunner` (Agent A).
3. **No Unrelated Code Refactoring**:
   - Agents must follow surgical change principles: touch only lines necessary for their assigned work packages.
