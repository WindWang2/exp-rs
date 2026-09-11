# PERFORMANCE — bounds, budgets, and measured evidence

Environment for all numbers below unless stated otherwise:
Linux 6.18 x64, GCC 16.2.1, **Release** (-O3 -DNDEBUG), Ninja `-j2`,
system GDAL/Qt6/QGIS. Debug-vs-Release numbers are never compared.

## Standing resource rules (enforced by construction)

- Window reads: default 1 GiB double budget (`kDefaultWindowBudgetBytes`),
  typed error above budget; streaming via tile walks.
- readMask: peak = one band's window (not bands × window).
- Range cache memory: LRU byte budget (default 64 MiB), bounded single fetch
  (default 8 MiB), block granularity 64 KiB.
- 9.0 additions carry their own caps: disk cache size cap + LRU; global
  in-flight fetch byte bound; STAC query cache bounded entries/bytes;
  catalog query bounded page/output sizes; statistics fallback scan bounded
  by row cap.

## Evidence log (appended as milestones land)

| Milestone | Benchmark / bound | Result |
|---|---|---|
| M0 | (none — correctness milestone; no hot-path changes; readMask gains a per-band dtype check = O(1)) | — |
