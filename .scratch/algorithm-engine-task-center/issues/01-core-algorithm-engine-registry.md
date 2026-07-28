# 01 — Core AlgorithmEngine Registry & TaskAlgorithmAdapter Framework

**What to build:** The backend `AlgorithmEngine` registry managing native `QgsProcessingAlgorithm` instances and `TaskAlgorithmAdapter` wrappers for custom tasks, covered by unit tests in `test_algorithm_engine.cpp`.

**Blocked by:** None — can start immediately.

**Status:** completed

- [x] Implement `AlgorithmEngine` singleton registry in `src/processing/framework/algorithm_engine.{h,cpp}`
- [x] Implement `TaskAlgorithmAdapter` in `src/processing/framework/task_algorithm_adapter.{h,cpp}`
- [x] Add unit tests in `tests/test_algorithm_engine.cpp` verifying algorithm registration, descriptor queries, and parameter schema validation
