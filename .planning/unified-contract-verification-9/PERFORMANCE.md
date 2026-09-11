# PERFORMANCE — resource bounds & evidence

## Resource budget (host: 16 CPU / 62 GB RAM; builds ≤ -j4, tests sequential)

- Contract lib build: small (5 TUs, no Qt, no GDAL) — seconds.
- Scanner runtime: linear in source bytes of `src/operators/**` (~2-3 MB) +
  app command sites; bounded by explicit file-size cap per TU (skip >
  2 MB with `scan_unresolved` finding) to keep worst case bounded.
- Graph generation: N = operators (~150+) × params — trivial memory (< 50 MB
  target including jsoncpp overhead).
- `benchmark_contract9` records: operator count, source bytes scanned,
  scan+graph+serialize wall time, peak RSS (getrusage), JSON output.

## Evidence (filled after implementation)

- PENDING: benchmark JSON + environment (git SHA, build type, compiler).

## Non-goals

- No product-runtime cost: the platform runs only in tests/tooling; zero
  changes to hot paths of execution, rendering, or I/O.
- No Debug-vs-Release comparisons; benchmarks run Release-only.
