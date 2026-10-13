# Direction C Performance & Resource Observatory

- **Baseline Environment**: Linux 6.8.0-60-generic x86_64
- **Concurrency Enforcements**:
  - `CMAKE_BUILD_PARALLEL_LEVEL=2`
  - `CTEST_PARALLEL_LEVEL=1`
  - Compiler launcher: ccache `/usr/bin/ccache`

## Test Execution Performance Summary

| Test Target | Test Cases | Assertions | Wall Clock Time | Status | Peak RSS |
|---|---|---|---|---|---|
| `test_exprs_ipc` | 26 | 143 | 0.12s | PASS (×2) | < 45 MB |
| `test_science_context_broker` | 34 | 155 | 0.28s | PASS (×2) | < 60 MB |
| `test_execution_plane_9` | 17 | 883 | 1.15s | PASS (×2) | < 95 MB |
| `test_runtime_python_channel_r4` | 9 | 75 | 27.8s | PASS (×2) | < 80 MB |
| `test_runtime_pool_recovery_r5` | 14 | 214 | 108.9s | PASS (×2) | < 85 MB |
| **Total** | **100** | **1470** | — | **100% GREEN** | — |

## Resource Containment Guarantees
- IPC request queue capacity bounded to prevent unbounded buffering (`maxQueuedRequests = 1024`, backpressure E6008).
- Zero zombie worker processes leaked on spawn error or pool shutdown (verified by `test_runtime_pool_recovery_r5` fail-closed test).
- Zero lingering run flocks or locks after workflow completion (verified by `test_execution_plane_9` M7 explain dump).
