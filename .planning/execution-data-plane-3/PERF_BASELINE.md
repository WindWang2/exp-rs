# PERF BASELINE — to be measured (Phase A)

Harness: `tests/test_execution_benchmarks.cpp` (Catch2, deterministic fixtures)
+ `scripts/perf_report.py` (JSON out, before/after diff).

Metrics per workload: wall_ms, cpu_ms, peak_rss_mb, read_bytes, write_bytes,
cache stats. Hardware: 16 logical CPU, 62 GB RAM, NVMe, GCC 16.2.1 Release,
offscreen Qt.

| Workload | Baseline | After C (fused) | After F (tile cache) | After I (catalog) |
|---|---|---|---|---|
| spectral_index streaming | TBD | | | |
| qa_mask | TBD | | | |
| majority/recode | TBD | | | |
| temporal composite (6 scenes) | TBD | | | |
| remote COG read (local http) | n/a | | TBD | |
| multi-step DAG (qa→mask→ndvi→thr) | TBD | TBD | | |
| cache-hit DAG | TBD | | | |
| model tile inference (fake) | TBD | | | |
| DataManager 100k query/list | TBD | | | TBD |

Rules: no gates on absolute numbers; trends only; each claim cites the JSON in
`benchmarks/`. med-fan: 3 repeats, median.
