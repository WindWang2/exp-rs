# PERFORMANCE — resource bounds & evidence

## Resource budget (host: 16 CPU / 62 GB RAM; builds ≤ -j4, tests sequential)

- Contract lib build: small (5 TUs, no Qt, no GDAL) — seconds.
- Scanner runtime: linear in source bytes of `src/operators/**` (~2-3 MB) +
  app command sites; bounded by explicit file-size cap per TU (skip >
  2 MB with `scan_unresolved` finding) to keep worst case bounded.
- Graph generation: N = operators (~150+) × params — trivial memory (< 50 MB
  target including jsoncpp overhead).
- `benchmark_contract9` records: per-phase wall time with counts in the
  detail field (operators, params, nodes/edges, output bytes), plus
  environment headers (os, cpu_cores, build type). Peak RSS is not
  recorded (follow-up).

## Evidence (Release, GCC, linux, 16 cores — exp.bench.contract9.v1)

- operator_param_scan: ~1511 ms over 136 operator registrations
  (whole-tree source scan, per-file cap 2 MB)
- descriptor_projection: ~7 ms for 136 schemas / 936 params
- graph_assembly: ~1816 ms → 824 nodes / 263 edges
- graph_serialize: ~5 ms → ~218 KiB canonical JSON
- Product-runtime cost: zero (tooling/test-side only).

## Non-goals

- No product-runtime cost: the platform runs only in tests/tooling; zero
  changes to hot paths of execution, rendering, or I/O.
- No Debug-vs-Release comparisons; benchmarks run Release-only.
