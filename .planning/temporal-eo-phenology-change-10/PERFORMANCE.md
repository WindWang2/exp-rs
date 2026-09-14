# PERFORMANCE — temporal-eo-phenology-change-10

Evidence-only numbers (benchmark_temporal10, full tier, Debug build, shared
16-core host under concurrent-track load ~16; never a gate):

| Benchmark | Scale | Throughput | Total |
|---|---|---|---|
| regularize | 1000 irregular obs -> 16-day calendar (linear) | 19112 series/s | 10 ms / 200 iters |
| harmonic_breaks | 500-sample joint fit (2 harmonics, ≤3 breaks) | 1227 fits/s (~0.82 ms/fit) | 82 ms / 100 iters |
| region_reduce | 100000 regions, one date | 62 dates/s | 808 ms / 50 iters |
| phenology_cycles | 10 years weekly, 2 cycles | 4137 series/s | 48 ms / 200 iters |

Scale verdicts:

- 100k-region target: the reducer passes ~6.2M region-samples/s — a
  100-date × 100k-region extraction accumulates in ~1.6 s of reduce time
  (I/O dominates separately). O(R) accumulation confirmed by construction
  (flat CSR scratch, no per-date allocation).
- harmonic_breaks is the expensive kernel (~0.8 ms/pixel at 500 samples):
  a 256×256 tile costs ~50 s. Documented honestly as the dense per-segment
  fit cost; the operator streams tile-by-tile so memory stays bounded, and
  the working-set guard refuses oversized gathers. Optimization (shared
  Gram across pixels is NOT possible — per-pixel series differ — but the
  per-call vector allocations can be hoisted) is accepted debt, recorded in
  REVIEW_LOG disposition.

Artifact: `benchmarks/temporal10.json` (schema exp.bench.temporal10.v1,
gitignored output tree; numbers above are the pinned evidence snapshot).
