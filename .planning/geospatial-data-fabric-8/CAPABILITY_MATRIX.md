# CAPABILITY MATRIX — after 8.0 (verified unless noted)

Statuses: **Certified** (executable evidence in this repo) / Implemented /
Partial / Refused-by-contract / Missing. "Locally verified" = executed on
Linux/GDAL 3.13.3 with the listed suites.

## A. Remote identity contract
- Validator states Fresh/Unknown/Stale/Offline + weak/strong classification — Implemented (7.0), regression-green.
- Comparison semantics (Unchanged/Changed/Inconclusive + decidedBy reasons) — Implemented (7.0), regression-green.
- Canonical identity + optional digest via `remoteIdentityToken`/`remoteIdentityBasis` — **Certified** (test_io_identity: stability, change-on-change, credential-shape stripping).
- Full-content digest — Refused-by-contract (never download large assets for identity).

## B. Execution-cache identity bridge
- Geospatial token resolver installed at hosts (app/CLI) — Implemented; wiring test in test_temporal_workspace (executed post-build, see TEST_MATRIX).
- Collectors fingerprint unregistered remote inputs through tokens; fail-closed otherwise — same suite.
- Host policy layering (set-once, replaceable) — Implemented.

## C. Range cache 8.0
- Handler lifetime (GDAL-owned heap handler) — **Certified** (ASan-clean suite; P0 fix).
- Concurrency dedup, adjacency/overlap, transient reset fallback, generation invalidation, COG overview/window reads with byte accounting — **Certified** (test_io_range_cache 13/13).
- updateEntrySize unknown-size guard — Implemented + reviewed; not fixture-reachable (documented limitation).

## D. STAC production surface
- Search/pagination/bounded searchAll — Implemented (7.0), regression-green.
- UTC normalization (mixed offsets, assumed-UTC) — **Certified** (test_io_stac_client 14/14).
- Deterministic ordering + duplicate-acquisition reporting — **Certified** (same suite).

## E. Multidim EO cubes
- Numeric coordinate axes, windows, missing values (7.0) — regression-green.
- String/datetime axes capture + JSON symmetry (incl. 7.0 toJson drift fix) — **Certified** (test_io_multidim 9/9, netCDF-4 gated).
- Exact label / equal-instant selection, typed misses — **Certified** (same suite).
- End-to-end bounded EO workflow (instant → window → maxCells refusal) — **Certified** (same suite).

## F. GeoParquet
- Certified round-trip (fields, null/empty, polygons, projected CRS, null geometry, atomic staged publish) — **Certified** (test_io_vector_interop 7/7, Parquet-driver gated).
- GDAL `CreateCopy` into Parquet — Refused (broken/unavailable in several builds; foundation writer used instead).
- Streaming write of very large feature counts to Parquet — Partial (bounded-batch writer contract applies; not separately certified at Parquet scale).

## G. Cloud/object credentials
- Credential-safe boundary documentation — Implemented (docs/io/cloud-credentials.md).
- Token-level credential stripping — **Certified** (test_io_identity).
- Redaction display for /vsis3-class spellings — Implemented (ResourceUri, 5.0) with identity/CLI coverage in test_cli_commands_json (post-build).

## H. Data Doctor 3.0
- cacheability / reproducibility / resampling_risk / multidim_axes — **Certified** (test_io_doctor 8/8).
- Auto-repair — Refused-by-contract (advice-only preserved).

## I. CLI / projections
- `data identity [--revalidate]`, `data cache check [--bytes N]` — Implemented + e2e loopback tests (post-build execution listed in TEST_MATRIX).
- STAC search CLI — Refused this track (network search surfaces belong to the data/agent tracks; `data stac` file mapping already exists).
