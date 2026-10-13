# Performance Observatory — Direction D

## Resource Budget & Hard Limits
- Build concurrency: Strict `-j2` (`CMAKE_BUILD_PARALLEL_LEVEL=2`).
- Test execution: Strict `-j1` (`CTEST_PARALLEL_LEVEL=1`).
- Platform: Headless Linux (`QT_QPA_PLATFORM=offscreen`).
- Maximum resident memory target during tests: < 300MB.

## Verified Test Execution Performance

| Target | Test Cases | Assertions | Elapsed Time | Parallelism | Peak RSS Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `test_help_core` | 13 | 2,156 | ~0.08s | `-j1` | Normal (< 50MB) |
| `test_i18n` | 6 | 401 | ~0.04s | `-j1` | Normal (< 50MB) |
| `test_workflow_pipeline_ui` | 18 | 135 | ~0.15s | `-j1` | Normal (< 80MB) |
| `test_experiment_studio_dock` | 5 | 65 | ~0.85s | `-j1` | Normal (< 120MB) |

## UI Optimization Verification
- **ExperimentStudioDock Matrix Table Batch Loading**:
  - Gated `setSortingEnabled(false)` and `setUpdatesEnabled(false)` during bulk row insertion.
  - Eliminated repetitive sorting re-evaluations and intermediate repaint cycles on `QTableWidget`.
- **Workflow Pipeline Editor Transactional IO**:
  - Temporary file atomic rename prevents corruption of on-disk workflow definitions.
- **Terminology Provider Deduplication**:
  - Linear pre-computation of unique descriptor IDs eliminates O(N^2) collision resolution penalties.
