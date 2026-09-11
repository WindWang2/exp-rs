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
| (to be filled at M1/M8) | | | | | |
