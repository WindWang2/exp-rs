# Performance & Scalability Baseline: exp-rs R7 Execution Plane

**Date**: 2026-10-01  
**Milestone**: M0  
**Context**: Throughput, Queue Ingestion, Aging Overhead & Resource Limits  

---

## 1. Current Baseline Characteristics (Master @ `1e28de8677`)

### 1.1 Ingestion Throughput & Queue Limits
- **Pending Queue Capacity**:
  - `m_maxPendingTasks` default is set to `4096` (`task_center.cpp:328`).
  - Attempting to submit 10,000 tasks under current default causes tasks $4097 \dots 10000$ to be rejected with primitive `-1`.
  - In PR #1423, tests in `test_execution_plane_7.cpp` expanded `m_maxPendingTasks` to allow 10k ingestion, exposing scheduler CPU bottlenecks.
- **Short-Job Drainage**:
  - With mock/noop worker tasks, 1,000 tasks drain in $\approx 280\text{ms}$.
  - 10,000 tasks without optimizations suffer quadratic latency degradation due to the aging sweep.

### 1.2 The Aging Sweep Bottleneck (`applyAgingSweepLocked`)
- **Location**: `src/processing/framework/task_center.cpp:2364-2374`
- **Current Behavior**:
  ```cpp
  const QList<long> candidateIds = m_readySerial.keys();
  unsigned int promoted = 0;
  for ( long taskId : candidateIds )
  {
      ...
      const int effective = effectivePriorityLocked( task, now );
      if ( effective >= task.effectivePriority )
          continue;
      ...
  }
  ```
- **Complexity Analysis**:
  - Invoked from `processNextQueuedTasks`, which runs on every task submission (`enqueueTask`) and completion (`onJobRecord`).
  - On each invocation with $K$ pending tasks in `m_readySerial`, it creates a heap-allocated `QList<long>` containing all $K$ keys.
  - It iterates over all $K$ entries, performing map lookups and clock checks.
  - Across a burst of $N = 10,000$ queued tasks, total operations scale as $\sum_{i=1}^N i \approx \frac{N^2}{2} = 50,000,000$ loop iterations with $10,000$ heap allocations.
- **Observed Impact**:
  - Scheduler lock contention spikes to $> 85\%$ CPU on core threads.
  - Thread starvation occurs for interactive queries during queue bursts.

### 1.3 Memory Footprint & Allocations
- **Task Center State Size**:
  - `AlgorithmTaskInfo`: $\approx 480\text{ bytes}$ per record.
  - 10k tasks in memory: $\approx 4.8\text{ MB}$ raw task metadata, plus heap node overhead for `QMap<long, AlgorithmTaskInfo>` ($\approx 32\text{ bytes}$ per node).
  - Overall RSS impact for 10k task metadata is well within limits ($< 25\text{ MB}$), confirming memory is not the primary bottleneck—the CPU scan and lock duration are.

### 1.4 Observability & Telemetry Overhead
- `ExecutionTelemetry`:
  - Counter increments (`increment(Counter::...)`): $\approx 8\text{--}12\text{ ns}$ per operation (atomic `fetch_add` with `memory_order_relaxed`).
  - Zero/near-zero overhead confirmed when events are not enabled.
  - `counters()` creates an immutable `std::map<std::string, uint64_t>`: $\approx 2.5\text{ µs}$ for all 40 counters.

---

## 2. Hard Resource Limits & Compilation Discipline

Under project rules and user instructions, execution resources are strictly bounded:

| Resource Dimension | Constraint Value | Enforcement Mechanism |
|---|---|---|
| **Build Concurrency** | Max 2 threads (`-j2`) | `export CMAKE_BUILD_PARALLEL_LEVEL=2`<br>`cmake --build <dir> -j2` |
| **Test Concurrency** | Max 2 test targets (`-j2`) | `export CTEST_PARALLEL_LEVEL=2`<br>`ctest --test-dir <dir> -j2` |
| **Sanitizer / Stress** | Max 1-2 threads | Strictly `-j1` or `-j2` on ASan / ThreadSanitizer |
| **Display Mode** | Offscreen headless | `QT_QPA_PLATFORM=offscreen` |
| **RAM Utilization** | RSS $< 4\text{ GB}$ | Monitored during builds; step down to `-j1` if system RSS $> 70\%$ |

---

## 3. R7 Performance Targets

1. **Ingestion & Drain**:
   - 10,000 rapid short tasks drain completely in $\le 5.0\text{ seconds}$ with mock workers.
   - Zero leaked task IDs, zero ghost tasks in history, zero budget drift (`allocated == 0` upon drain).
2. **Aging Sweep Optimization**:
   - Introduce rate-limiting / interval throttling (e.g. minimum 100ms between full scans) or dirty tracking so `applyAgingSweepLocked` executes in $O(1)$ when no age-out is due.
   - Eliminate heap allocation of `m_readySerial.keys()` on every pass.
3. **Interactive Latency**:
   - Interactive job submitted into a queue of 1,000 batch jobs is dispatched within $\le 50\text{ms}$.
