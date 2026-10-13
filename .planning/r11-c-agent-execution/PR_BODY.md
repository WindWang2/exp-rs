## Summary

This PR hardens Direction C: Pi/MCP Agent execution closure, IPC boundaries, Python worker lifecycle, and Workflow concurrency in `exp-rs` without introducing second frameworks or product creep (#1448, #1450, #1462).

### 1. IPC Channel Validation & Bounded Queue Flow Control (WP-C5, WP-C10)
- **Fail-closed empty request methods**: Requests with empty method names fail immediately with typed `E6002` protocol error rather than timing out ambiguously.
- **Queue capacity & backpressure**: Added `maxQueuedRequests` (default 1024) to `IpcChannel::Options`. Incoming requests past the capacity limit are dropped and immediately answered with typed retryable `E6008` backpressure error.
- **Oracle**: Tested RED vs GREEN in `tests/test_exprs_ipc.cpp` (2 new test cases; 143 assertions pass).

### 2. Python Worker Node Thread Safety & Zombie Node Prevention (WP-C6)
- **Atomic lifecycle fields**: `WorkerNode` fields (`isBusy`, `isRestarting`, `crashBudgetLeft`, `restartCount`) converted to thread-safe `std::atomic` types.
- **CAS acquisition**: `acquireWorker` uses `compare_exchange_strong` on `isBusy`, eliminating cross-thread check-then-mark race.
- **Fail-closed process spawn**: `createWorkerNode` explicitly validates `node->worker->startWorker(...)`. On startup failure, allocations and servers are cleaned up immediately and `nullptr` returned, preventing dead zombie worker nodes from polluting `m_nodes`.
- **Oracle**: Tested RED vs GREEN in `tests/test_runtime_pool_recovery_r5.cpp` (new fail-closed test case; 214 assertions pass).

### 3. Workflow Terminal Persistence & Ownership Handover Synchronization (WP-C3, WP-C4)
- **Happens-before gap closed**: Identified and resolved the root cause of the flaky `test_execution_plane_9` M7 explain dump failure (`runLocks held: 1` expected `0`).
- **RAII handover tracking**: Introduced `TerminalHandoutGuard` and `m_inFlightTerminalHandouts`. `drainPersists()` now synchronizes both `m_inFlightPersists == 0` AND `m_inFlightTerminalHandouts == 0`, ensuring that `releaseRunOwnership` has completely released `m_locksByRunId` before `drainPersists()` unblocks.
- **Oracle**: `tests/test_execution_plane_9` 883 assertions pass (GREEN × 2).

## Verification Matrix
- `test_exprs_ipc`: 143/143 passed (×2)
- `test_runtime_pool_recovery_r5`: 214/214 passed (×2)
- `test_runtime_python_channel_r4`: 75/75 passed (×2)
- `test_execution_plane_9`: 883/883 passed (×2)
- `test_science_context_broker`: 155/155 passed (×2)
- Total assertions verified: 1470 passed (100% green)
- Independent review rounds 1 & 2: PASS (0 P0, 0 P1)
- `git diff --check`: clean (0 whitespace/formatting issues)
