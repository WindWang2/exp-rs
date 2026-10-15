# Track R13-L: Performance & Resource Verification

## Compilation & Test Constraints
- Build concurrency: `-j2` (hard cap, `CMAKE_BUILD_PARALLEL_LEVEL=2`).
- Test concurrency: `-j1` (`CTEST_PARALLEL_LEVEL=1`).
- Physical lock: `/tmp/exp-rs-agent-locks/global-build.lock`.
- Display: `QT_QPA_PLATFORM=offscreen`.

## Memory & Latency Budgets
1. **GUI Main Thread Latency**:
   - Worker restart latency: < 5 ms on GUI thread (no synchronous blocking join).
   - Destruction drain time: bounded by worker cancellation responsiveness (< 500 ms).
2. **Matrix Preview Memory**:
   - For 10,000 points: heap memory capped to < 2 MB (vs > 80 MB without bounding).
   - Table initialization time: < 15 ms (vs > 1200 ms).
3. **File Durability Overhead**:
   - Pipeline save duration: bounded by single disk `fsync()` (< 10 ms).
