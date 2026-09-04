# ARCHITECTURE — Execution & Data Plane 3.0

Design rule: **additive seams first**. TaskCenter remains the dispatch source of
truth (ADR 0062/0123). Nothing rewrites the spine; each phase adds a capability
behind an interface and flips behavior via conservative defaults.

## Layer map (after epic)

```
 surfaces: GUI | Agent/MCP | CLI | WorkflowRunCoordinator
            \           |        |      /
             ▼          ▼        ▼     ▼
             ExecutionPlane / TaskCenter      (unchanged contract surface)
               │ admission: TaskResourceBudget2 (multi-dimension, Phase G)
               │ dispatch:  ChunkGraphRouter (Phase B/C) or JobEngine (default)
               │ identity:  ExecutionFingerprint v2 (unchanged; Phase E adds
               │            content-digest inputs where available)
               │ results:   ExecutionResultCache2 ──► ArtifactStore (D/E)
               ▼
 JobEngine (unchanged) ──► LocalWorkerHost (Phase K, opt-in isolation)
        │                        └─ WorkerProtocol v1 (framed JSON + payload paths)
        ▼
 RSOperators ── file-level (legacy) ── chunk-native (opt-in via ChunkOperator)
        │
        ▼
 GDAL runtime ── RemoteCogTileCache (Phase F: memory LRU + disk LRU +
        │            ETag revalidation + request coalescing, /vsicurl/ integrated)
        ▼
 DataManager ── WorkspaceCatalog (Phase I: SQLite-backed, paged queries,
                in-memory hot index, model/view for GUI)
```

## Key decisions

1. **Chunk graph (B/C)**: `src/runtime/chunk/` new Qt-free library. Contracts:
   `ITileProducer/ITileConsumer/BoundedTileQueue<TileJob>/TileWindow(halo)/
   ChunkGraphRunner`. `ChunkGraphRunner` schedules tile jobs across a small
   bounded thread set; file-level fallback wraps a fused chain into one task.
   Materialization only at: cache boundary, checkpoint boundary, explicit user
   output, fan-out, random-access dependency, memory pressure (budget callback).
   RSOperators declare `chunkCapability()` = {FileOnly, ChunkNative, Hybrid}.
   First chunk-native operators: spectral index + QA mask + threshold (elementwise,
   provable equivalence).
2. **ArtifactStore (D)**: `src/data/artifact_store.{h,cpp}`; SQLite metadata +
   payload-on-fs; IDs are uuids; path resolution API for GDAL consumers;
   refcounts fed by DataManager assets, cache leases, checkpoints. GC consults
   ArtifactStore instead of path heuristics where available.
3. **Cache (E)**: extend ExecutionResultCache with optional persistent
   content-addressed store (`~/.rs_studio/cache/`): objects by SHA-256 digest,
   entries keyed by fingerprint hex; serve = hardlink-or-copy from object pool +
   digest verify; conservative env-gated default (`SICNU_ARTIFACT_CACHE=1`).
   Wrong-path overwrite protections inherited from existing path-claims + new
   digest checks. Transactional serve (temp+rename) preserved.
4. **Remote COG cache (F)**: `src/data/providers/remote_tile_cache.{h,cpp}` +
   GDAL config integration: per-process disk tile cache dir fed via
   `CPL_VSIL_CURL_CACHE_SIZE`/`VSI_CACHE` + app-level coalescing layer for
   repeated range reads (dedup by URL+range), ETag/Last-Modified remembered per
   URL in SQLite, bounded prefetch, offline fallback to stale tiles.
   GUI-thread rule: no network IO on app thread (enforced by deferred open path).
5. **Scheduler 3.0 (G)**: `TaskResourceBudget2` with `ResourceRequest
   {cpu_threads, ram_mb, vram_mb, disk_read_w, disk_write_w, network_w, gpu_device,
   latency_class}`; latency classes {Interactive, Background, Batch, Gpu, Io,
   Network}; UI reserved capacity (interactive lane never fully starved); aging
   prevents starvation; override per request; telemetry counters per dimension.
6. **GPU plane (H)**: `src/runtime/gpu/` Qt-free: GPUDeviceManager (enumerate via
   existing model-runtime backends), ModelSessionPool keyed by model id+device,
   VRAMBudget admission, InferenceBatchQueue (dynamic batching window),
   OOM → device fallback → CPU fallback ladder. Operators request sessions via
   context; never load/destroy per call.
7. **Workspace catalog (I)**: `src/data/workspace_catalog.{h,cpp}` SQLite;
   DataManager gains an optional catalog backend (hydrated lazily, batched
   mutations); GUI panel converts to model/view with paged fetch (no per-asset
   widgets at 100k). 250k-record design headroom, WAL, prepared statements.
8. **Persistent workflow runtime (J)**: extend checkpoint store with
   `runs.sqlite` (run/step/task history, schema-versioned, migratable);
   TaskCenter state snapshots persisted per transition (task table + pipeline
   table); fix W1-W4 (persist-before-dispatch for tracked pipelines, validate
   resumed outputs by size+mtime+optional digest, delete ghost checkpoints
   atomically via marker file, run history retention).
9. **Worker host (K)**: `src/runtime/worker/` local process worker
   (`sicnu_worker` executable) speaking length-prefixed JSON over stdin/stdout
   (protocol versioned); coordinator watches liveness, SIGKILL ⇒ task failed
   (never app crash); used for OTB/Python/GPU-prone operators via executor
   registration; opt-in per operator family.
10. **Observability (L)**: `src/runtime/observability/ExecutionTelemetry`:
    ring-buffer events (submission, admission wait, queue wait, resource wait,
    dispatch, cache hit/miss, chunk progress, RSS/VRAM samples, IO counters,
    worker status, artifact lifecycle) → JSON dump API, CLI summary, GUI
    diagnostic panel hook, agent inspection tool. Off-hot-path (lock-free-ish
    single-writer ring, atomic counters; never blocks execution).

## Compatibility

- All legacy file-level operators keep working unchanged (default router).
- Existing fingerprint/cache tests must keep passing (contract superset).
- Checkpoint JSON v1 readers stay loadable; v2 adds fields, migrations versioned.
- QGIS/GDAL consumers always get real paths (ArtifactStore resolves to paths).
