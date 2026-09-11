# PERFORMANCE — resource bounds & budgets

Environment for all numbers: Linux, Ninja/Release, gcc, host 16 CPUs /
62 GB RAM, parallelism ≤ -j4 build / -j1 tests (shared host with other
tracks). Debug-vs-Release numbers are never compared.

## Existing solver budgets (unchanged contracts)

- relaxation: ≤ 24 hard passes, ≤ 8 soft passes
- unsat-core: ≤ 8 candidates, subsets ≤ 3, ≤ 32 simulations per search
- decisions ledger ≤ 256 entries; document ≤ 2000 items; issue list ≤ 500

## 9.0 additions and their bounds

- **Trace**: ≤ 24 entries × bounded move counts — O(passes) extra memory.
- **Oscillation history**: 2-pass state per constraint — O(constraints).
- **Scoped re-solve**: restricts work; never exceeds the full solve cost.
  Repair loop uses it to keep later passes at O(subset), not O(document).
- **fit_content.text_ref**: one `fitTextIntoBox` call per fit_content
  evaluation (same cost class as wrap-aware preflight; kMaxFitIterations
  bounded; kMaxWrappedLines=64).
- **Master furniture**: clone count ≤ pages(10) × furniture items —
  bounded by the existing 2000-item document cap; compile is linear in
  cloned items.
- **page_break/continuation**: O(1) per declaration; continuation labels
  resolve in one post-pass over pages (≤ 10).
- **diff_templates**: bounded output (JSON-pointer list capped at 200
  deltas, truncation reported).
- **Export**: synchronous, one layout at a time; atomic via temp+rename;
  no background workers introduced anywhere in this track.

## Benchmark evidence

Recorded in PERFORMANCE.md updates per milestone with build type, data
size, and host context. Baseline for the repair loop on the 8.0 corpus:
covered by `benchmark_quality7` (existing target) — re-run before/after
the scoped re-solve change and recorded below.

### Results

| Benchmark | Build | Input | Baseline (master) | After 9.0 | Ratio |
|---|---|---|---|---|---|
| condition_evaluate (mapspec conditions) | Release, this host | 200k iterations | n/a (same code as master) | 777,818 ops/s | — |
| condition_validate | Release, this host | 100k iterations | n/a | 1,292,150 ops/s | — |
| test_mapspec full suite wall time | Release, this host | 206 cases / 2492 assertions | (8.0 corpus was 60 cases; not comparable) | 16.4 s wall, 15.0 s user | — |

Solver-evidence additions are bounded-overhead by construction: the trace
couples to the existing 24-pass loop (≤ 32 cids per entry), oscillation
derivation scans the last trace entry, and the scoped re-solve is a strict
subset of the full solve. The chart-over-map repair sweep is capped at
64 candidate positions; the repair ledger costs exactly one extra
preflightMapSpec per repair pass (preflight itself is bounded by the
existing 500-issue and 24-pass caps). No Debug-vs-Release comparisons are
made; all numbers above are Release on the same host.
