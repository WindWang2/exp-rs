# BASELINE — audit of latest master (2026-09-10)

## Git state

- `origin/master` = `322dfd3876c34ed62b42846598cacd711c8c91d6` — identical to the
  planning snapshot; no execution-time drift.
- Open PRs: none. Open issues: none (`gh pr list --state open`, `gh issue list` empty).
- Latest 40 merged PRs mapped: the 5.0/6.0/7.0 goal-series branches (#736–#832) are
  merged residue; #833–#835 are post-merge CI repairs of master
  (#834 bridges `range_cache` VSI APIs across GDAL 3.8–3.13 — visible in
  `openReadonlyVsi`/`Open` overrides of `src/geospatial/remote/range_cache.cpp`).
- Surviving remote branches: all merged into master (feature branches of the 7.0
  series + older zcode/* goal branches). None genuinely divergent; none to resurrect.

## Overlap map (what recently merged work already covers)

| Track ask | Already shipped by | Verdict |
|---|---|---|
| Remote identity contract (validators, states, redaction) | #823 (`feat/cloud-geospatial-io-7`) → `remote_source_validator.*`, tests `test_io_remote_validator.cpp` | Implemented; 8.0 only enriches |
| Bounded HTTP fetch, retry/byte budgets | #823 → `http_fetch.*` | Implemented |
| Range cache w/ LRU, coalescing, stale policies, telemetry | #823 → `range_cache.*`, tests `test_io_range_cache.cpp` | Implemented; 8.0 adds fault evidence + one hazard fix |
| STAC search/pagination/filters/temporal adapter | #823 → `stac_client.*`, `test_io_stac_client.cpp` | Implemented; datetime normalization missing |
| Multidim coordinate slicing, windows, missing counts | #744-era + #823 → `multidim_view.*`, `test_io_multidim.cpp` | Implemented; string/datetime axes missing |
| Atomic publish (raster+vector) | #771/#823 → `atomic_fs.*`, `vector_writer.*`, `raster_writer.*` | Implemented |
| Doctor v2 sections | #823 → `data_doctor.*` | Implemented; additive checks for 8.0 |
| Execution fingerprint V2 + result cache | #770/#824 (data track) → `execution_fingerprint.*`, `task_center.cpp` | Implemented; remote-identity seam dead |
| Execution identity resolver seam | #828 (execution plane 7.0) → `execution_identity_resolver.*` | SEAM ONLY — no collector consults it (verified by grep: only definition + cpp) |

## Local environment (evidence base)

- Linux 6.18.49-2-lts x64, zsh; cmake 4.4.3; ninja (existing `build/` cache).
- GDAL 3.13.3 system install; drivers verified locally: netCDF (rw+uv), HDF5 (ro),
  Zarr (rw+uv), **Parquet (rw via `ogr_Parquet.so` plugin — write verified with a
  tiny point layer)**, GPKG/GeoJSON/Shapefile/FlatGeobuf. Some *unrelated* plugins
  fail to load (PDF→libpodofo, MySQL→libmariadb) — irrelevant to this track.
- Resource policy: `CMAKE_BUILD_PARALLEL_LEVEL=2`, targeted builds, tests `-j1`.

## Verified capability matrix ( Implemented / Partial / Missing )

- **A Remote identity**: Implemented (probe/revalidate/states/redaction/JSON round-trip;
  weak-vs-strong honesty). Missing: canonical identity surface on the identity struct;
  optional content digest. (Partial)
- **B Execution-cache bridge**: **Missing** (seam dead; remote unregistered inputs fail
  fingerprinting as "unresolved input" — conservative but no reuse ever).
- **C Range cache**: Implemented; hazard found: `Open` Unchanged-path
  `updateEntrySize(entry, identity.sizeBytes)` writes `hasSize=true` even when the
  revalidation answer proved nothing about size (identity.hasSize false ⇒ fabricated
  size 0). Fault coverage gaps: concurrent readers, connection resets mid-fetch,
  changed-content-under-same-URL read path, COG overview reads through cache.
- **D STAC**: Implemented search surface. **Missing**: UTC normalization (verbatim
  strings; `buildTemporalSeries` compares raw ISO strings — mixed offsets order wrong);
  duplicate-acquisition handling; deterministic tie-break beyond stable_sort order.
- **E Multidim**: Implemented for numeric axes. **Missing**: string/datetime axis
  values (common in EO cubes: `time` as CF datetime strings); no end-to-end bounded EO
  workflow evidence.
- **F GeoParquet**: Partial — read "Accessible", writer refuses-by-profile; local GDAL
  proves write capability. Missing round-trip evidence.
- **G Cloud credentials**: Partial — redaction exists (userinfo, credential-shaped
  query); no documented provider-neutral boundary; no /vsis3-class redaction tests.
- **H Doctor**: Implemented core. Missing: cacheability verdict, multidim availability
  posture, modern-vector posture, reproducibility advice checks.
- **I CLI**: Partial — `data` has inspect/doctor/probe/capabilities/product/stac(file
  only); no identity probe, no cache surface, no STAC *search* surface.

## Cross-track seams (shared-file risk)

- `src/processing/framework/task_center.cpp` + `src/processing/algorithms/temporal/temporal_workspace.cpp`
  — owned by execution-plane/data tracks; 8.0 touches ONLY the input-identity fallback
  in `fingerprintInputsForOperatorParams` (narrow, additive).
- `tests/CMakeLists.txt` — appending only (comment at line ~7191 explicitly reserves
  appended region for concurrent tracks).
- `src/app/main.cpp` — startup install lines only.
