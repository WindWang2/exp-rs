# BASELINE — Cloud-Native Geospatial Data Fabric 9.0

Branch: `feat/geospatial-data-fabric-9`
Worktree: `/home/kevin/projects/rs-studio/exp-rs-geospatial-data-fabric-9`
Base: `origin/master` @ `132da5e998` (merge of PR #847, feat/geospatial-data-fabric-8)

## Repository state at baseline (2026-09-11)

- `origin/master` = `132da5e998eca004d43285d1f71565f973030f5f`.
- 8.0 series fully merged: #837–#847 (model-runtime-8, verification-platform-8,
  cartography-platform-8, execution-plane-8, spatial-scientist-harness-8,
  dataset-experiment-mlops-8, plugin-platform-8, professional-workbench-8,
  scientific-processing-8, geospatial-data-fabric-8).
- Local `master` in the MAIN worktree is `origin/master` + one UNPUSHED commit
  `8f6293bceb` ("fix(core): resolve P0 defects … #848-#852") plus ~35 uncommitted
  modified files owned by parallel tracks. **This branch does not depend on any
  of that local-only state** and branches strictly from `origin/master`.
- Open PRs: none.
- Open issues: #848–#882, all `ready-for-agent`.
- Remote branches: `-5/-6/-7/-8` families are merged-PR residue (their content
  is in master). Two sibling `-9` tracks already exist
  (`feat/execution-concurrency-lifecycle-9`, `feat/scientific-algorithms-9`);
  neither touches `src/geospatial/**` or `src/operators/io/**` (verified by
  ownership table in OWNERSHIP.md).

## Baseline evidence: this host

- GCC 16.2.1, CMake 4.4.3, Ninja; Qt6 + QGIS + GDAL system packages.
- Baseline build in the worktree: `ninja -C build -j2 sicnu_geospatial` → OK
  (33/33 targets, static lib `libsicnu_geospatial.a`).

## What 8.0 already delivers (verified by reading current master code)

| Area | State on master |
|---|---|
| Remote identity | `remote_identity_token.cpp` — fail-closed strong-ETag basis, credential-shaped query stripping, VSI-prefix unwrapping, SHA-256 token `:v1:` |
| Range cache | `range_cache.cpp` (1180 lines) — LRU byte budget, block coalescing, per-resource fetch mutex (concurrent dedup), stale policies (RevalidateOnOpen/ValidateOnce/TrustForever), telemetry (hits/misses/bytes/coalesced/evictions/invalidations/fallbacks/revalidations), /vsicurl/ fallback |
| STAC | `stac_client.cpp` — GET/POST search, bbox/intersects/datetime/collections/ids/limit, query ext, sortby best-effort, rel=next pagination incl. POST-merge, maxItems bounded crawl, UTC-normalized series order, duplicate reporting, credential-redacted display |
| Multidim | `multidim_view.cpp` — lazy listing, index slices, coordinate-value exact/nearest, string-label equal-instant selection, spatial window reads, cell budgets, block shapes |
| Vector | `vector_reader` streaming batches + attribute projection + WHERE + bbox filter + CRS transform + opt-in exact count; `vector_writer` staged atomic dataset-group publish with transactions |
| Raster | window/full budgeted reads, tile walks, block reads with edge padding, overview selection + resampled reads, fidelity-gated integer writes, staged atomic publish |
| Atomic FS | staged temp → fsync → validate → rename; sidecar-first/main-last group publish; #791 backup discipline; #807 cross-device fallback |
| Doctor | cacheability / reproducibility / resampling_risk / multidim_axes checks |
| Columnar | GeoParquet read+write round-trip certified (driver-gated) |

## Genuine gaps found for 9.0 (drives the milestones)

1. **#850 on this base**: `VectorWriter` move ctor/assign drop
   `mTransactionActive` → GPKG/PG commit skipped at finalize, GDALClose
   implicitly rolls back, staged features silently lost while the (nearly
   empty) file still publishes. CONFIRMED by code read
   (`src/geospatial/vector/vector_writer.cpp:197-220`).
2. **#874 on this base**: `readMask` compares stored Float32-widened pixels to
   the declared NoData with strict double equality
   (`src/geospatial/raster/raster_reader.cpp:330`); sentinels that are not
   float-exact (e.g. -9999.9) never match.
3. **Writer move/transaction audit**: only VectorWriter carries transaction
   state; no other writer moves state. RasterWriter move is safe (verified).
   But cancel() relies on GDAL's implicit rollback — make it explicit.
4. **Float32 write overflow**: `RasterWriter::writeWindow` fidelity-gates
   integer dtypes only; double→Float32 overflow silently produces ±inf.
5. **Directory fsync**: publishStagedFile fsyncs file contents but not the
   containing directory after rename (POSIX durability of the rename itself).
6. **No local-file identity**: identity tokens are remote-only; local assets
   (the majority in workspaces) cannot participate in cache/fingerprint
   identity (M1).
7. **Range cache 9.0 gaps**: no read-amplification metric; no bounded global
   in-flight fetch concurrency; truncated-response (short read) handling must
   be proven not cached; no retry/backoff evidence; adjacent+overlap dedup
   exists but no explicit metric.
8. **No disk layer for the range cache** (M3): process-memory only; large
   logical COGs cannot keep working sets across processes.
9. **STAC**: relative asset hrefs not resolved against item self/canonical
   URL; no bounded client-side query cache (M4).
10. **Multidim**: no 4D/5D logical-cube assembly with variable/band roles and
    JSON descriptor round-trip (M5).
11. **Vector**: no extent/statistics pushdown surface; FlatGeobuf not
    certified in the format profiles (driver-truth gated) (M6).
12. **No catalog query primitives** (M7) for Workbench/DataManager to adopt.
13. **No data-locality hints contract** (M8) for the execution plane.
14. **Doctor**: no GDAL capability-matrix surface; no scale evidence (M9).
