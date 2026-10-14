# R13-H Performance Benchmarks & Bounding Ledger

## Resource Bounds and Discipline
- Compiler parallel jobs: `-j2` (strictly capped)
- CTest concurrency: `-j1` (strictly serialized)
- QPA platform: `offscreen`
- Memory monitoring: Peak RSS and memory limits strictly checked.

## Micro-Benchmarks & Complexity Bounds (WP-16)

### Capability Scaling Benchmark
- **Scale targets**:
  - 50 capabilities
  - 500 capabilities
  - 5000 capabilities
- **Metrics recorded**:
  - `p50` planning latency
  - `p95` planning latency
  - Peak RSS during planning
- **Bound**: Search and ranking must maintain sub-quadratic complexity $O(N \log N)$ or better. No nested quadratic loops over capabilities.

### Asset Scaling Benchmark
- **Scale targets**:
  - 10 assets
  - 100 assets
  - 1000 assets
- **Metrics recorded**:
  - `p50` classification latency
  - `p95` classification latency
  - Peak RSS
- **Bound**: $O(N)$ linear scaling for asset classification and precondition validation.

### Baseline vs Post-Hardening Tracking
| Metric | Baseline (Pre-Hardening) | Post-Hardening | Acceptance Delta |
|---|---|---|---|
| Planner test suite execution (189 tests) | ~6.5 sec | 6.54 sec | <= +1% |
| Planner schema & bounds validation latency | 0.00 sec | 0.00 sec | 0% |
| Broker cache key transition & synthesis | 0.03 sec | 0.03 sec | 0% |
| Repair planner candidate dedup & cost rank | 0.02 sec | 0.02 sec | 0% |
| Peak RAM during test suite | < 200 MB | < 200 MB | Verified |

