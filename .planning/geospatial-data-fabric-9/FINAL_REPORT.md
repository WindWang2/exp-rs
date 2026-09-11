# FINAL REPORT — Cloud-Native Geospatial Data Fabric 9.0

Branch: `feat/geospatial-data-fabric-9` (worktree from `origin/master` @
`132da5e998`; final sync against the master that landed #848-#852 and
#853-#882 fixes is part of the PR)

## What 9.0 delivers (per milestone)

- **M0 — Data Integrity First**: #850 fixed with evidence (move transfers the
  transaction; explicit rollback owns cancel semantics; commit failure rolls
  back before close). #874 fixed in the band's STORAGE precision (Float32
  sentinels compare in float space — no absolute epsilon that would mis-mask
  Float64 scientific data). Float32 write overflow is a typed FidelityLoss
  (was silent ±inf). POSIX directory fsync after publish. Main-phase
  group-publish failure test proves sidecar rollback.
- **M1 — Unified Asset Identity**: `li1` local tokens (canonical path + size
  + mtime + inode + budget-bounded content hash; honest content/metadata
  strength; fail-closed), `sd1` subdataset qualification, unified dispatch
  reusing the 8.0 `ri1` remote tokens verbatim.
- **M2 — Range/COG 9.0**: global in-flight fetch byte bound (admission gate,
  no starvation, peak gauged), truncated-206 gate (a short body against the
  echoed Content-Range window is now a typed error → /vsicurl/ fallback;
  was accepted and cacheable — a torn-block poison), dedup + amplification
  + in-flight telemetry.
- **M3 — Hierarchical Cache**: checksummed disk block layer under the memory
  cache — content-identity keyed (unprovable identity never disk-cached),
  atomic publication, bounded LRU eviction, lock-free reads, write-through
  after memory insert; clearEntries/uninstall drop disk too; doctor-visible
  stats.
- **M4 — STAC 9.0**: item delivery provenance; relative asset hrefs resolve
  against it (remote RFC 3986 merge — including a fixed latent
  "host//path" defect for root-relative references; local lexical merge with
  directory containment); opt-in bounded query cache (entries/bytes/TTL).
- **M5 — EO Cubes**: lazy 4D/5D cube descriptors; axis instants from string
  datetime labels AND numeric CF-relative units (all-or-nothing resolution);
  exact JSON round-trip symmetry; typed structural violations.
- **M6 — Vector & Columnar**: extent(allowScan) without full-table
  surprises; driver-evaluated field statistics (nulls honest, numeric-only,
  quoted identifiers); batch feature writes with index-named failures;
  FlatGeobuf certified through a real filtered-streaming round-trip
  (runtime driver-gated); io:inspect schema truth (#880 runtime half).
- **M7 — Catalog Primitives**: detached immutable AssetRecord + predicate/
  temporal/spatial/sort/pagination engine with hard caps and bounded
  summaries; STAC and canonical adapters; explicitly NOT a store.
- **M8 — Locality Hints**: locality/kind/bytes/seek-cost/chunk-shape/
  compression/identity-cacheability as a pure JSON-stable function; no
  invented facts; no scheduler.
- **M9 — Doctor/Scale**: gdalCapabilityMatrix (version + per-profile live
  driver CREATE/CreateCopy/Open truth); concurrent window-read correctness
  over a 64-tile synthetic raster; FD-bound lifecycle evidence.

## Verification evidence (local, Release, GCC 16.2.1, -j2)

**Post-merge final regression: 27/27 io-family suites green** against the
merged tree (origin/master containing the #848-#852 and #853-#882 fixes).
Highlights: test_io_vector_contract 12 cases/100329 assertions,
test_io_range_cache 20 cases/122 assertions incl. disk layer + both
truncation gates, test_io_roundtrip_matrix 8 cases/639, test_io_multidim
12/235, test_io_stac_client 20/136, test_io_catalog_query 9/50,
test_io_hints 4/27, test_io_scale9 3/214.
Regression property proven for the two critical fixes: re-introducing the
#850 defect makes the new test fail (verified once, then fixed again).

## Adversarial review

Round-0 self-review findings + dispositions in REVIEW_LOG.md; subagent
review findings and remediations appended in the same file.

## Known limitations / honest divergences

- #874 merge note: master's fix adds an absolute 1e-6 tolerance for ALL
  dtypes; this branch keeps band-storage-precision comparison (no absolute
  epsilon) because a magnitude-blind tolerance can mis-mask legitimate
  Float64 near-sentinel values and large-magnitude integer sentinels. The
  divergence is deliberate and documented; tests pin both behaviors that
  matter (float-exact miss on old code; Float64 exactness unchanged).
- Local asset href resolution refuses paths escaping the item directory
  (absolute hrefs are the escape hatch) — a documented security default.
- STAC query cache keys carry raw URLs in-process only (never logged/
  persisted); display surfaces stay redacted.
- The ShortRange/ResetRanged fault behaviors are transient by design so the
  /vsicurl/ fallback remains servable against hostile origins.
- CI/CD was NOT awaited (per direction policy); completion evidence is the
  local reproducible test/bench record above. Not run on this host: OTB,
  ONNX-runtime-dependent suites, GPU paths.
