# ARCHITECTURE — 9.0 deltas inside the existing authority map

Immutable invariants (unchanged): Pi is the only agent loop;
`WorkflowRunCoordinator → TaskCenter → JobEngine → Executor/Operator` the only
scheduling chain; QGIS the only render authority; `src/geospatial` the only
core geospatial I/O authority; existing Dataset/Experiment stores own metadata;
existing registries own capability registration.

## New 9.0 pieces (all inside src/geospatial, all leaf modules)

```
src/geospatial/
  identity/local_identity.{h,cpp}      # M1: local file identity tokens
  remote/range_cache_disk.{h,cpp}      # M3: optional disk block layer
  catalog/asset_query.{h,cpp}          # M7: query primitives over records
  hints/data_locality.{h,cpp}          # M8: locality/execution hints
  multidim/multidim_cube.{h,cpp}       # M5: 4D/5D cube descriptor
```

### M0 — no new modules; surgical fixes
- VectorWriter: move transfers ALL state (`mTransactionActive` included);
  cancel() performs explicit `GDALDatasetRollbackTransaction` when a
  transaction is active before closing (deterministic, not driver-fortune);
  finalize() clears transaction state on every exit path.
- RasterReader::readMask: sentinel comparison in the BAND's storage precision
  (Float32 bands ⇒ float-space compare; Float64 ⇒ exact double). NaN nodata
  unchanged.
- RasterWriter::writeWindow: Float32 target bands reject |v| > FLT_MAX and
  non-finite float-overflowing values with ErrorCode::FidelityLoss (same
  doctrine as the integer gate).
- atomic_fs::publishStagedFile: POSIX path fsyncs the target directory after
  rename — best-effort and silent by design: several legitimate filesystems
  (network/FUSE) reject directory fsync with EINVAL, and failing the publish
  there would trade a real capability for a durability nicety. The
  file-content fsync remains the correctness gate; this narrows the crash
  window for the directory entry.

### M1 — identity
`localIdentityToken(path, opts)` → basis = canonical path + size + mtime_ns +
first-N-bytes SHA-256 (budget-bound; 0 = metadata-only, flagged weak) →
`sha256(basis)`. Result struct carries `strength: strong|metadata-only` and
`hashBytes`. Fail-closed: unreadable/inaccessible file ⇒ "" (never cacheable).
Remote tokens unchanged; STAC asset identity = remoteIdentityToken(asset href)
with item-qualified context; multidim = local/remote token + subdataset path
qualifier. Execution seam: same token string contract as 8.0
(`valueDomain=file_identity` for locals) — no new bridge required.

### M2/M3 — cache
- RangeCacheConfig gains `maxConcurrentFetchBytes` (global in-flight bound;
  0 = unlimited-for-tests) and `disk` block-store hook.
- Disk store: content-addressed by (resource identity token, block index);
  per-block SHA-256 trailer; torn/partial blocks fail checksum ⇒ treated as
  miss (never served); size cap + LRU file eviction; atomic single-rename
  block publication; no network under the store's lock (fetch happens outside,
  publish takes the lock only to insert).
- Telemetry adds: `bytesFetchedFromDisk`, `readAmplification`
  (= bytesFetched / bytesServed), `dedupedRangeReads`.

### M4 — STAC
- `StacClient::resolveAssetHref(item, asset)`: relative hrefs resolve against
  the item's self/link base (RFC 3986 merge), result keeps credential redaction.
- `StacQueryCache`: bounded (entries + bytes) LRU keyed by canonical
  (method, canonical-url, sorted-body-hash); TTL; explicit clear; stats.

### M5 — cube
`MultidimCube` = immutable descriptor (variables with roles, axis dims incl.
datetime axes with normalized instants, spatial CRS/transform) + `toJson()` /
`fromJson()` round-trip symmetry tests + assembly from MultidimView metadata.
No data materialization (views stay lazy).

### M6 — vector
- `VectorWriter::writeFeatures(batch)` — single transaction-friendly batch.
- `VectorReader::extent()` (declared layer extent, no scan) and
  `fieldStatistics(field, {min,max,sum,count,unique-bounded})` via OGR SQL
  aggregate pushdown with documented driver-dependent evaluation and a bounded
  fallback scan (row cap; overshoot = typed error, never a silent full scan).
- FlatGeobuf: certified format profile row when the runtime driver advertises
  CREATE; round-trip test gated at runtime (GDAL truth), honest skip otherwise.

### M7 — catalog
`AssetRecord` (detached, immutable, JSON-serializable) +
`AssetQuery` (predicates: id/collection/datetime range/bbox/role/media-type/
arbitrary metadata equality) + `queryRecords(records, query, pageBounds)` —
pure functions, bounded outputs, deterministic ordering. No I/O, no store.

### M8 — hints
`DataLocalityHints` struct + `dataLocalityHintsFor(path)` synthesizing from
inspect/COG/remote probes already in the module. Pure, additive, no scheduler
coupling; execution plane may consume the JSON projection.

### M9 — doctor/scale
- `data doctor` gains `capability_matrix` (GDAL version + per-driver
  CREATE/CreateCopy/virtual-IO truth) and disk-cache reporting.
- Scale fixtures: 100k synthetic AssetRecords query bench; concurrent window
  reads over a synthetic tiled COG; FD-count bound assertion.
```

## Threading rules preserved
- Range cache: no network fetch while holding the store lock (fetch outside,
  insert under lock); disk store likewise.
- All new public entry points thread-safe or documented single-thread like the
  writers (writers stay non-thread-safe by contract, as today).
