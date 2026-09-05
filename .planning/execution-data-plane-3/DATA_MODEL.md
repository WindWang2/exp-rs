# DATA MODEL — Data Plane 3.0

Existing identity model (kept, extended — never broken):

- `AssetId` + `AssetRevision` (catalog identity, ADR 0125)
- `ExecutionFingerprint` = SHA-256 digest, contract v2 (params canonical, inputs
  revision-aware, producer-chained, destination-free)
- `DerivationRecord` (lineage on asset)
- `WorkflowRun` checkpoint JSON v1 (whole-run aggregate)

## New concepts introduced by this epic

### ArtifactId / ArtifactStore (Phase D)
```
ArtifactId        = stable uuid (assigned once at first registration)
ArtifactVersion   = monotonic per-ArtifactId counter
ProducerFingerprint= ExecutionFingerprint hex that produced this version
ContentDigest     = SHA-256 of payload bytes (computed lazily/optionally;
                    mandatory for cache-served payloads)
StorageLocation   = absolute filesystem path (QGIS/GDAL still need paths)
Metadata          = kind, size, mtime, band structure summary, codec
Lineage           = input ArtifactId@Version list + algorithm id/version + params digest
References        = {dataManagerAssetIds, cacheLeases, checkpointRefs, liveRunRefs}
Lifecycle         = {state: Live|Retained|Trash, createdAt, lastTouch, reclaimAfter}
```
Storage: SQLite metadata DB (`workspace_catalog.sqlite3`) + filesystem payloads.
Logical artifact ≠ physical path; path stays derivable for GDAL/QGIS consumers.

### Content-addressed cache layer (Phase E)
```
CacheKey      = ExecutionFingerprint hex
CacheEntry    = {outputs: [{artifactId, digest, size}], resultPayload (canonical JSON),
                 storeTime, inputSnapshot stats, sourceRunId}
DigestStore   = content-addressed payload pool under
                ~/.rs_studio/cache/objects/<aa>/<digest>  (refcounted, leased)
```
Conservative defaults: only fingerprints that already pass ADR 0124 gates are
eligible; content validation (digest) required for cross-session serve; a serve
that cannot verify content falls through to real execution. Never GC leased or
referenced content.

### Chunk contracts (Phase B)
```
TileId     = {rasterId, z/x/y window + halo spec, band set}
ChunkToken = opaque handle; ChunkProducer/ChunkConsumer via BoundedChunkQueue
Backpressure = bounded queue capacity; producers block; consumers cancel via token
```
File-level operators remain the default; chunk-native operators opt in.

### Persistent catalog (Phase I) & workflow runtime (Phase J)
```
SQLite tables: assets, aliases(path), derivations, collections, temporal_scenes,
               tags, runs, run_steps, checkpoints_meta
Schema version table + migration; WAL journal; batched transactions.
```

## Invariants (test-backed, §18 of goal)

- I1 same fingerprint ⇒ same scientific semantics (params/inputs/revisions).
- I2 destination path never enters nor corrupts identity; serving to a different
  path yields byte-equivalent payloads.
- I3 input revision change ⇒ downstream invalidation (chained fingerprints).
- I4 cache hit semantically equivalent to fresh run (payload equality modulo
  cache annotations; content digest verified).
- I5 provenance consistent on fresh / cache / resume / worker / chunk paths.
- I6 resume ≡ fresh execution semantics.
- I7 GC never reaps active artifacts / leased cache content / live checkpoints.
