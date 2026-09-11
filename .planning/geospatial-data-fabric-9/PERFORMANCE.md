# PERFORMANCE — bounds, budgets, and measured evidence

Environment for all numbers below unless stated otherwise:
Linux 6.18 x64, GCC 16.2.1, **Release** (-O3 -DNDEBUG), Ninja `-j2`,
system GDAL/Qt6/QGIS. Debug-vs-Release numbers are never compared.
(Host note: final builds ran under heavy parallel-track load (~23 load
average from sibling worktrees); only PASS/FAIL and byte counts are claims —
wall-clock timings from this window are NOT recorded as benchmarks.)

## Standing resource rules (enforced by construction)

- Window reads: default 1 GiB double budget (`kDefaultWindowBudgetBytes`),
  typed error above budget; streaming via tile walks.
- readMask: peak = one band's window (not bands × window).
- Range cache memory: LRU byte budget (default 64 MiB), bounded single fetch
  (default 8 MiB), block granularity 64 KiB.
- **9.0 M2**: global in-flight fetch byte bound (default 64 MiB; observed
  peak exposed as `max_in_flight_fetch_bytes` and asserted as an ABSOLUTE
  first-position gauge ≤ the cap with a workload whose ungated sum exceeds
  the cap — deleting the gate fails the test); read amplification =
  bytesFetched/bytesServed reported and asserted > 0 after real misses,
  never fabricated.
- **9.0 M3**: disk layer capped by `diskMaxBytes` with amortized LRU
  eviction (walk bounded at 200k entries, triggered by the put-accounted
  estimate only when over cap); reads lock-free; block publication does its
  file I/O OUTSIDE the store mutex — the mutex covers name allocation and
  stats/eviction only; no network I/O under any cache lock.
- **9.0 M4**: STAC query cache bounded by entries (32) AND bytes (16 MiB,
  compact-serialized accounting — an accounting convention, not a resident
  RSS bound) with TTL; cache mutex never spans the network fetch; key =
  canonical method+URL+body hash, in-process only.
- **9.0 M5**: cube descriptors read NO array data; axes bounded at
  kMaxAxisValues (4096) with explicit `*Bounded` truncation flags.
- **9.0 M7**: catalog queries bounded by page limit + hardCap (typed
  ResourceExhausted on cap breach in count-off mode); 100k-record query test
  with 25-record pages passes in-process.
- **9.0 M8**: hints never read pixel/feature data (statistics stay off);
  remote facts come only from the bounded 1 KiB probe.

## Evidence log

| Milestone | Benchmark / bound | Result |
|---|---|---|
| M0 | (correctness milestone; no hot-path change; readMask adds an O(1) per-band dtype check) | — |
| M1 | local identity: hash budget honored (default 8 MiB prefix); hashedBytes asserted | test_io_identity |
| M2 | in-flight peak ≤ declared bound (absolute first-position gauge, ungated workload would exceed cap); dedup_hits > 0 under same-resource racing; amplification > 0 after real misses | test_io_range_cache (admission + concurrent cases) |
| M3 | disk-cold read after clearEntries costs ~0 window bytes from origin; cap eviction keeps on-disk bytes ≤ cap | test_io_range_cache (disk cases) |
| M5 | describeCube = metadata-only (no array read by construction) | test_io_multidim (cube cases) |
| M6 | statistics = explicit opt-in driver aggregate (scan documented in contract; never called by inspect paths) | test_io_vector_contract |
| M7 | 100k-record query: page of 25 from 100 matches, bounded summary over 100k | test_io_catalog_query (scale case) |
| M9 | 4-thread window reads over 64-tile synthetic raster byte-correct; 200 open/close cycles drift FD count ≤ +4 | test_io_scale9 |
