# Direction C Test Plan

## Verification Targets and Existing Test Suites
1. **IPC & External Process**:
   - Target: `test_exprs_ipc`
   - Scope: Frame read/write, length prefixes, oversize payload rejection, EOF detection, protocol compatibility.
2. **Model Runtime**:
   - Target: `test_model_runtime_stress`
   - Scope: Concurrency, session reuse, VRAM accounting, eviction, failure broadcasting.
3. **Execution Plane & Workflow**:
   - Target: `test_execution_plane_9`
   - Scope: Workflow DAG execution, task status transitions, ownership release, observability, terminal state persistence.
4. **Python Worker & Plugin Lifecycle**:
   - Target: `test_runtime_python_channel_r4`
   - Scope: Worker acquisition, start failure cleanup, process recycling, restart budget thread safety.
   - Target: `test_plugin_lifecycle_halffail_r4`, `test_exprs_plugin_loader`
5. **Agent Ops & Science Context**:
   - Target: `test_agentops_*`, `test_science_context_*` (or targeted test executables in tests/ CMake)
   - Scope: Evidence ledger persistence, recovery replay, broker cache key with typed failures.

## Verification Protocol
- Every bug fix or hardening enhancement MUST follow the RED -> Minimal Fix -> GREEN x 2 protocol.
- No sleep-based workarounds; thread synchronization must use explicit primitives (mutexes, condition variables, atomics, Qt signal/slot synchronization).
- Parallel execution: build targets with `-j2` (`CMAKE_BUILD_PARALLEL_LEVEL=2`), run ctest with `-j1` (`CTEST_PARALLEL_LEVEL=1`).
- Headless Qt tests must run with `QT_QPA_PLATFORM=offscreen`.
- Review verification: Independent reviewer check diff against `origin/master...HEAD` with 0 P0 and 0 P1 issues.
