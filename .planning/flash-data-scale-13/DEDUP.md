# DEDUP — flash-data-scale-13

Rule (track protocol §1.7): if live master / an open PR already implements a work package,
do not redo it. Dynamic re-check at every milestone (baseline done / implementation half /
pre-review / pre-PR) with `scripts/dev/overlap_scan.py`.

## Already implemented on master — NOT this track's work

| Capability | Where | Why it is not a gap |
|---|---|---|
| Perf observatory harness + gates + report/verify tooling | `tests/perf/perf_observatory.h`, `tests/test_perf_observatory.cpp`, `tests/test_perf_io_observatory.cpp`, `scripts/bench/perf_observatory_report.py` (#1125) | Measurement exists; this track consumes it and adds scale oracles, it does not rebuild it |
| Durable path index (`aliases` table, `byPath()`) + paged filtered query | `src/data/workspace_catalog.{h,cpp}` | Separate durable catalog mirror with its own consumers; not the runtime DataManager index |
| Governance `aliases` table + `assetByPath()` + `idx_gov_assets_source` | `src/data/governance/governance_store.cpp` | Durable-side alias lookup; the runtime `DataManager::findByPath` has no index — that is the gap |
| Keyset cursor pagination pattern (`QueryCursor` encode/decode, `WHERE (k,v) > (?,?)` seek) | `src/data/query_cursor.{h,cpp}`, consumers `src/dataset/dataset_store_samples.cpp`, `src/experiment/experiment_store.cpp` | The pattern exists and is proven; governance simply does not use it — wire it in, do not reinvent |
| Real `COUNT(*)` entity totals | `GovernanceStore::entityCounts()` (#758-3) | Keep semantics |
| DataManager thread-affinity contract (mutation owner-affine, snapshot-served readers, cross-thread lease release) | `src/data/data_manager.{h,cpp}` (#703/#800/#852) | Contract is the invariant this track PRESERVES while changing the snapshot representation |
| Atomic SQLite transactions in governance upserts | `governance_store.cpp` (#1105) | Keep |

## Real gaps this track closes (code evidence)

1. **`DataManager` population is O(N²)** — `Impl::publishSnapshot()`
   (`src/data/data_manager.cpp:214-223`) copies the live `records` vector into the snapshot
   (Qt COW assignment is a refcount bump, but the next live mutation detaches and deep-copies
   every `AssetRecord`). 24 mutation sites publish; population of N assets pays ~N detach
   copies of N records. Evidence: `benchmarks/observatory/obs_dataset_register_scaling.json`
   (exponent ≈ 2.25). Fix: immutable chunked record store + O(1) publication.
2. **`DataManager::findByPath` is a full scan with per-record identity work**
   (`src/data/data_manager.cpp:821-872`) — per record it allocates a `QStringList`
   (`virtualPathAliases`) and performs `QFileInfo::canonicalFilePath()` (filesystem syscalls).
   Evidence: `obs_dataset_find_by_path_hotspot.json` + `_scaling.json`. Fix: generation-scoped
   path→record key index computed once per mutation, O(1) probe.
3. **`GovernanceStore::query()` deep paging is OFFSET-rescan dominated** —
   `SELECT COUNT(*)` + `SELECT … ORDER BY <key> LIMIT ? OFFSET ?`
   (`governance_store.cpp:2504-2520`), no unique tiebreak in any ORDER BY, no index on any
   ORDER BY key (`updated_ms`, `display_name`, `acquisition_ms`), `total` recomputed on every
   page, `offset` unbounded. Evidence: `obs_governance_paging_scaling.json` (worst doubling
   ≈ 1.71–1.77; ledger Round 8 "REAL hotspot found"). Fix: composite `(sort_key, pk)` indexes,
   keyset/seek pagination via the existing `QueryCursor` pattern, explicit count semantics,
   panel consumer switched to cursor paging.
4. **No 100k-scale structural oracles for the DataManager catalog** — existing ladders stop at
   20k assets (dataset) / 100k rows (governance). Fix: 1k→10k→100k ladder with structural
   counters (record copies, canonicalization calls, index probes), not wall-clock gates.
5. **No concurrency/lifetime oracle for the new structures** — multi-reader + mutation writer
   against the chunked store and the path index. Fix: reader/writer stress with snapshot
   lifetime + generation rollover assertions.

## Overlap with open PRs — none semantic

- #1137 (geospatial maintenance): `src/geospatial/**` only. No shared file.
- #1136 (classroom safety): labs registry. No shared file.
- #1135 (temporal phenology): no `src/data/**` change, BUT it regenerates
  `benchmarks/observatory/*.json` and `benchmarks/perf-observatory-baseline.md`. This track
  will also regenerate those records from the same command after the fixes → expected textual
  conflict if #1135 merges first; last-writer-wins regeneration is the documented resolution
  (records are derived artifacts; the verify script is the authority).
- Shared append-only hotspots for ALL of these: tail of `tests/CMakeLists.txt`, `.gitignore`,
  `.goal-loop-ledger.md` (tracked), `.planning/<track>/` (whitelisted per track).

## Pivot record

None required — no work package of this track was pre-empted by live master or an open PR.
