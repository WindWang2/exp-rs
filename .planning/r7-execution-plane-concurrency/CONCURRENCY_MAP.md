# Concurrency Architecture Map: exp-rs R7 Execution Plane

**Date**: 2026-10-01  
**Milestone**: M0  
**Context**: Concurrency, Object Lifetime & Snapshot Publication  

---

## 1. Subsystem Architecture & Concurrency Boundaries

The exp-rs Execution Plane coordinates multi-threaded and multi-process job execution, admission gating, telemetry, and durable workflow DAG execution.

```
                                  ┌───────────────────────────────┐
                                  │      Client Applications      │
                                  │ (Workbench UI, CLI, Scripts)  │
                                  └───────────────┬───────────────┘
                                                  │
                                                  ▼
                                  ┌───────────────────────────────┐
                                  │     RsJobRunner / Adapter     │
                                  └───────────────┬───────────────┘
                                                  │
                                                  ▼
┌───────────────────────────────┐                 │
│    WorkflowRunCoordinator     ├─────────────────┼──────────────────────────────┐
│  - Pipeline DAG State Machine │                 │                              │
│  - Checkpoint IO (m_checkpt)  │                 ▼                              │
│  - In-Flight Persist Tracking │     ┌───────────────────────┐                  │
└───────────────┬───────────────┘     │      TaskCenter       │                  │
                │                     │  - Admission Control  │                  │
                │ DirectConnection    │  - Ready PriorityHeap │                  │
                │ (taskUpdated)       │  - Resource Budgeting │                  │
                ▼                     └───────────┬───────────┘                  │
┌───────────────────────────────┐                 │                              │
│      ExecutionTelemetry       │                 ▼                              │
│  - Atomic Counters            │     ┌───────────────────────┐                  │
│  - Ring Buffer Trace Events   │     │    ExecutionPlane     │                  │
│  - Lock-Free Publication      │     │  - Worker Process Rt  │                  │
└───────────────────────────────┘     │  - Fused Chain Exec   │                  │
                                      └───────────┬───────────┘                  │
                                                  │                              │
                                                  ▼                              │
                                      ┌───────────────────────┐                  │
                                      │       JobEngine       │                  │
                                      │  - Worker Thread Pool ◄──────────────────┘
                                      │  - Job Records & Sync │
                                      └───────────────────────┘
```

---

## 2. Component Concurrency Model & Locking Contracts

### 2.1 `TaskCenter`
- **Primary Mutex**: `mutable QRecursiveMutex m_mutex`
- **Internal Data Structures**:
  - `QMap<long, AlgorithmTaskInfo> m_tasks`: Master task repository.
  - `std::vector<ReadyEntry> m_readyHeap`: Binary min/max priority heap for admitted tasks awaiting resource availability.
  - `QMap<long, quint64> m_readySerial`: Ready entry serial deduplication index.
  - `QMap<std::string, long> m_taskByJobId`: Mapping between `JobEngine` jobId and `TaskCenter` taskId.
  - `ActiveResourceTotals m_active`: Running aggregate sums of allocated memory, CPU weights, and I/O slots.
- **Lock Discipline**:
  - `m_mutex` protects task state transitions, queue insertion/removal, and resource allocation tallies.
  - **Critical Rule**: `m_mutex` must NEVER be held when invoking external callbacks, emitting Qt signals (`taskAdded`, `taskUpdated`, `taskLogAdded`), or dispatching to `JobEngine::submitWithId` / `JobEngine::cancel`.
  - Signal queuing: Events are staged into `m_pendingSignals` under lock, and dispatched via `flushPendingSignals()` after `m_mutex` is unlocked.

### 2.2 `JobEngine`
- **Primary Mutex**: `std::mutex m_mutex`
- **Worker Management**:
  - Worker threads run `workerLoop()`, waiting on `std::condition_variable m_cv`.
  - `std::map<std::string, JobRecord> m_jobs`: Central registry of submitted, active, and completed jobs.
  - `m_listener`: Global job completion/progress callback.
- **Lock Discipline**:
  - `m_mutex` protects queue pop and job state mutation.
  - `notify(copy)` clones the record under `m_mutex`, drops `m_mutex`, and invokes `m_listener` outside locks.

### 2.3 `WorkflowRunCoordinator`
- **State Mutex**: `mutable std::mutex m_mutex`
- **Disk I/O Mutex**: `mutable std::mutex m_checkpointIoMutex`
- **Data Structures**:
  - `std::map<std::string, quint64> m_latestPersistSeq`: Tracks monotonically increasing persist requests per run.
  - `std::map<long, std::shared_ptr<WorkflowRun>> m_runsByPipeline`: Pipeline to run instance mapping.
  - `std::set<std::string> m_locksByRunId`: Mutual exclusion locks for active workflow runs.
- **Lock & Lifetime Discipline**:
  - `m_mutex` protects in-memory run state, step plan updates, and notification queues.
  - `m_checkpointIoMutex` serializes disk writes for atomic checkpointing and sweep operations outside `m_mutex`.
  - Background persists running on `JobEngine` worker threads must be tracked via an atomic in-flight counter (`m_inFlightPersists`) and condition variable (`m_persistCv`) so `drainPersists()` and `shutdownForTests()` can safely wait for completion before destruction.

### 2.4 `TaskResourceBudget2`
- **Data Structure**: Multi-dimensional capacity vectors:
  - `ramMb`: Physical memory ceiling and current allocation.
  - `vramMb`: GPU memory ceiling.
  - `cpuWeight`: CPU core concurrency slots.
  - `ioHeavy`: Dedicated I/O bottleneck slot bounds.
- **Invariant**:
  $$\forall t: 0 \le \text{allocated}(t) \le \text{capacity}$$
  Refused tasks consume 0 resources. Terminal tasks release 100% of allocated resources. Auto-retry must not double-count resource reservations.

### 2.5 `ExecutionTelemetry`
- **Telemetry Counters**: Indexed array of `std::atomic<uint64_t>` values.
- **Event Trace**: Circular buffer protected by internal mutex.
- **Publication Protocol**:
  - Counter increments/decrements are lock-free (`std::memory_order_relaxed` / `acquire`).
  - `counters()` creates an immutable point-in-time `std::map<std::string, uint64_t>` snapshot by value.

---

## 3. Reader / Writer Snapshot Semantics

| Interface | Method | Mutex Held During Read | Snapshot Semantics | Publication Protocol |
|---|---|---|---|---|
| `TaskCenter` | `admissionSnapshot()` | `m_mutex` (minimal) | Point-in-time | Copies precomputed `m_active` totals; $O(1)$ read. |
| `TaskCenter` | `explainDump()` | `m_mutex` | Point-in-time | Formats active tasks, ready heap size, and capacity counters. |
| `WorkflowRunCoordinator` | `explainRun(pipelineId)` | `m_mutex` (briefly) | Immutable snapshot | Clones `StepPlan` list under lock; formats string outside lock. |
| `WorkflowRunCoordinator` | `explainDump()` | `m_mutex` (briefly) | Immutable snapshot | Clones counts and run states under lock; formats outside lock. |
| `ExecutionTelemetry` | `counters()` | None (atomic reads) | Point-in-time | Iterates enum counters using `load(relaxed)` into value map. |

---

## 4. Teardown & Destruction Ordering

To prevent Heap-Use-After-Free and cross-singleton access during process termination and test resets, the teardown sequence is strictly ordered:

```
[Test Teardown / Process Exit]
            │
            ▼
1. CoordinatorFixture / WorkflowRunCoordinator
   - Disconnect TaskCenter::taskUpdated
   - drainPersists() (wait m_inFlightPersists == 0, flush m_checkpointIoMutex)
   - drainRunNotifications()
   - Clear m_runsByPipeline, m_latestPersistSeq, m_locksByRunId
            │
            ▼
2. JobEngine
   - shutdownForTests() (signal worker stop, join all worker std::threads)
   - clearExecutors()
            │
            ▼
3. TaskCenter
   - shutdownForTests() (drain pending tasks, reset ready heaps, clear queues)
   - Reset m_nextTaskId = 1, m_nextPipelineId = 1
            │
            ▼
4. ExecutionTelemetry
   - resetForTests() (zero atomic counters, clear event ring)
```
