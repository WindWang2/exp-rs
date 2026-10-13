# Risk Register (Direction C: Agent Execution & Runtime)

| Risk ID | Description | Severity | Likelihood | Mitigation / Guardrail |
|---|---|---|---|---|
| R-01 | Re-implementing fixes already covered in PR #1477 | High | High | Explicit cross-checking with #1477 file diff; focus strictly on residuals and distinct work packages (C1-C12). |
| R-02 | Memory/CPU starvation on shared build host | High | Medium | Strictly enforce `CMAKE_BUILD_PARALLEL_LEVEL=2` and `CTEST_PARALLEL_LEVEL=1`; build only targeted test binaries instead of all targets. |
| R-03 | Flaky tests due to cross-thread happens-before defects | High | High | Establish rigorous happens-before mechanisms (mutex, std::atomic, QFuture/wait conditions) rather than heuristic sleeps. |
| R-04 | State machine desynchronization (dual terminal states) | Critical | Medium | Enforce strict single-terminal state transition checks in `WorkflowRunCoordinator` and `TaskCenter`. |
| R-05 | IPC hanging or orphaned worker processes | High | Medium | Implement fail-safe watchdog timeouts and proper child process termination on crash/cancel. |
| R-06 | Hardware-dependent GPU/CUDA claims | Medium | High | Mark all unverified real-hardware features as `NOT_VERIFIED` and test via mock/software-fallback seams. |
