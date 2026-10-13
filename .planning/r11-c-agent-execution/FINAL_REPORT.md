# Direction C Final Engineering Report: Agent Execution Closure, Recovery & Model Runtime Hardening

- **Repository**: `WindWang2/exp-rs`
- **Branch**: `hardening/r11-c-agent-execution`
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r11-c-agent`
- **Base Commit**: `origin/master` (`8a08642760e2e8ffd2c4b965fc4683c01d3e4758`)
- **Completion Date**: 2026-10-13

---

## 1. Executive Summary
This hardening effort closes key reliability and concurrency boundaries across the Agent runtime, IPC stream protocol, Python isolated worker pool, and Workflow execution coordinator. Following Andrej Karpathy's guidelines (Think Before Coding, Simplicity First / YAGNI, Surgical Changes, Goal-Driven Verification), all changes were strictly localized, verified against reproducible RED-first oracles, and validated with double GREEN runs (GREEN × 2).

All work avoided colliding with open PRs (#1475, #1476, #1477, #1478) while resolving documented residuals and deep concurrency gaps.

---

## 2. Hardened Work Packages & Root Cause Analysis

### WP-C5 & WP-C10: IPC Channel Request Envelope Gate & Queue Backpressure
- **Problem**:
  1. Requests with an empty method name were queued indefinitely or caused timeout ambiguities instead of failing fast.
  2. Incoming request queues were unbounded, exposing the host to memory exhaustion when downstream processing lagged behind IPC arrival.
- **Root Cause & Fix**:
  - In `src/sdk/exprs/ipc_channel.cpp`: added an upfront fail-closed check for empty request methods, immediately returning typed `E6002` protocol error.
  - In `src/sdk/exprs/ipc_channel.h` & `src/sdk/exprs/ipc_channel.cpp`: added `maxQueuedRequests` (default 1024) to `IpcChannel::Options`. When incoming requests exceed the cap, requests are dropped, `mDroppedRequests` metric incremented, and callers immediately receive typed retryable `E6008` backpressure error.
- **Verification**: Verified RED on empty method; added tests `request with empty method fails closed immediately with E6002` and `incoming request queue cap bounds memory and answers backpressure E6008` in `tests/test_exprs_ipc.cpp`. 143/143 assertions pass (GREEN × 2).

### WP-C6: Python Worker Thread Safety & Zombie Process Elimination (#1462 Residual)
- **Problem**:
  1. In `PythonWorkerProcessPool`, `WorkerNode` lifecycle fields (`isBusy`, `isRestarting`, `crashBudgetLeft`, `restartCount`) were non-atomic and accessed across threads without synchronization.
  2. `createWorkerNode(int id)` ignored `node->worker->startWorker(...)` return value. When process spawn failed (e.g., bad python interpreter path), `createWorkerNode` returned a non-null node, leaking dead zombie worker nodes into `m_nodes` and causing `initialize()` to report success.
- **Root Cause & Fix**:
  - In `src/python/isolated/python_worker_process_pool.h`: changed `isBusy` and `isRestarting` to `std::atomic<bool>`, and `restartCount` and `crashBudgetLeft` to `std::atomic<int>`.
  - In `src/python/isolated/python_worker_process_pool.cpp`:
    - `acquireWorker`: implemented thread-safe atomic CAS loop with `compare_exchange_strong` on `isBusy`, eliminating cross-thread double-handout races.
    - `releaseWorker`: implemented atomic store and CAS loop for crash budget restoration.
    - `createWorkerNode`: verified `node->worker->startWorker(...)`. On failure, the node disconnects, disposes server and worker processes, deletes the node allocation, and returns `nullptr`, allowing `initialize()` to fail closed.
- **Verification**: Tested against RED (execve failure producing zombie nodes with `health.total == 2`); added unit test `Worker initialization with failing process spawn fails closed without stranding zombie nodes` in `tests/test_runtime_pool_recovery_r5.cpp`. 214/214 assertions pass (GREEN × 2). `test_runtime_python_channel_r4` 75/75 assertions pass (GREEN × 2).

### WP-C3 & WP-C4: Workflow Terminal Persistence & Ownership Handover Race (#1448, #1450)
- **Problem**:
  - `test_execution_plane_9` intermittently flaked on `explain dumps expose admission and run evidence (M7)` with `runLocks held: 1` expected `0`.
- **Root Cause & Fix**:
  - `WorkflowRunCoordinator::onTaskUpdated` (and companion terminal paths in `startTrackedPipeline`, `resumeRunImpl`, and `cancelRun`) persisted terminal checkpoints synchronously and then called `releaseRunOwnership(finalizedRunId)` outside `persistRun`.
  - However, `drainPersists()` on the main thread only checked `m_inFlightPersists == 0`. When `persistRun` finished, `m_inFlightPersists` dropped to 0 before `releaseRunOwnership` ran, allowing `drainPersists()` to return while `m_locksByRunId` was still held.
  - Fix: Added `std::atomic<int> m_inFlightTerminalHandouts` and `TerminalHandoutGuard` RAII synchronization. `drainPersists()` now waits until `m_inFlightPersists == 0 && m_inFlightTerminalHandouts == 0`.
- **Verification**: Verified on `test_execution_plane_9` across all test cases. 883/883 assertions pass (GREEN × 2).

---

## 3. Test Suite Pass Matrix (100% Green)

| Target | Suites / Cases | Assertions | Status | Concurrency |
|---|---|---|---|---|
| `test_exprs_ipc` | 26 | 143 | PASS × 2 | `-j2` build / `-j1` run |
| `test_runtime_pool_recovery_r5` | 14 | 214 | PASS × 2 | `-j2` build / `-j1` run |
| `test_runtime_python_channel_r4` | 9 | 75 | PASS × 2 | `-j2` build / `-j1` run |
| `test_execution_plane_9` | 17 | 883 | PASS × 2 | `-j2` build / `-j1` run |
| `test_science_context_broker` | 34 | 155 | PASS × 2 | `-j2` build / `-j1` run |
| **Total** | **100** | **1470** | **ALL GREEN** | |

---

## 4. Independent Review Sign-Off
- **Round 1 Review**: PASS (0 P0, 0 P1, 3 P2 resolved).
- **Round 2 Invariant Review**: PASS (0 P0, 0 P1, 0 P2).
- **Code Check**: `git diff origin/master..HEAD --check` clean (0 warnings).
- **Standards**: Strictly follows C++20 and Qt 6 conventions, RAII invariants, and lock hierarchy.
