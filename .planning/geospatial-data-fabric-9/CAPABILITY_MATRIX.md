# CAPABILITY MATRIX — 9.0 plan vs master state

Rule: for every planned capability, master must be PROVEN to lack it
(implementation / production caller / surface / tests) before we build.
"8.0 has X" claims below were verified by reading master code, not by trusting
8.0 docs.

| Capability (9.0 milestone) | Master state | Gap → 9.0 action |
|---|---|---|
| M0 writer move/transaction correctness | broken for VectorWriter (#850) | fix + explicit rollback + tests |
| M0 sentinel-precision masking | broken (#874) | fix + tests |
| M0 Float32 write overflow gate | missing (integer dtypes gated only) | add FidelityLoss gate |
| M0 dir-fsync durability | file fsync only | add POSIX directory fsync after publish |
| M0 atomic publish failure restore | present (#791/#807) | keep; add fault-injection tests for crash-between-phases at the GROUP level |
| M1 local file identity | absent (remote-only tokens) | new `local_identity` (hash-bounded, strength-declared, fail-closed) |
| M1 STAC asset identity | absent (item-level only) | per-asset token composition |
| M1 multidim subdataset identity | absent | subdataset-qualified token |
| M1 cache-key canonicalization | resource_uri canonical() exists | wire into identity + disk cache keys |
| M1 identity invalidation | remote: stale policies; local: none | local identity embeds mtime_ns+size+hash → change ⇒ new token |
| M2 range dedup/coalesce/telemetry | present | metrics: amplification, dedup counts |
| M2 bounded concurrent fetch | per-resource mutex only | add global in-flight byte/requests bound |
| M2 truncated response safety | unproven | short-read ⇒ error, never cached; fault tests |
| M2 retry/fallback | maxRetries + fallback exist | keep + backoff evidence test |
| M2 overview-aware window reads | GDAL via VSI + readWindowResampled | keep |
| M2 origin mutation | stale policies | keep + generation invalidation tests |
| M3 memory range cache | present | keep |
| M3 disk layer | absent | new optional disk block store: content-addressed, LRU/size-cap, checksum, pinned reads, torn-write safe |
| M3 doctor cacheability | present (memory) | extend to report disk layer |
| M4 STAC search/pagination/UTC | present | keep |
| M4 relative href resolution | absent | resolve against item self URL; display stays redacted |
| M4 query cache | absent | bounded TTL/size client cache keyed by canonical query |
| M5 multidim slices/windows | present | keep |
| M5 4D/5D cube assembly + roles + JSON round-trip | absent | new MultidimCube descriptor (variables↔band roles, axes) with symmetric JSON |
| M6 GeoParquet R/W, GPKG, streaming reads, filters | present | keep |
| M6 FlatGeobuf certification | absent | driver-truth gated certify (write/read round-trip when GDAL driver exists; honest skip otherwise) |
| M6 extent/stats pushdown surface | absent (exact count only) | extent() + field statistics via OGR SQL pushdown where driver supports, else documented scan |
| M6 write batching | per-feature API | add writeFeatures(batch) with one transaction flush |
| M7 catalog query primitives | absent | new `src/geospatial/catalog`: detached immutable AssetRecord + predicate/sort/pagination/spatio-temporal filters + bounded summaries; NO second store (in-memory engine over records callers already hold) |
| M8 locality hints | absent | new `src/geospatial/hints`: dataLocalityHintsFor(path) → remote/local, est bytes, chunk/window prefs, cacheability, seek cost, COG/multidim shape |
| M9 doctor capability matrix | absent | gdal driver capability matrix JSON (version + driver truth) |
| M9 scale evidence | benchmark_scale8 exists for 8.0 | new 100k-asset catalog bench, concurrent window reads, FD bounds |

## Duplicate-implementation guards (per /goal invariants)

- No second reader/cache inside agent/workbench — catalog consumes records,
  hints are pure functions over existing inspection paths.
- No second scheduler — M8 exports data only.
- No second renderer/store — M7 is engine-over-records, not a store.
