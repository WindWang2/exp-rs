# ARCHITECTURE — audited structure & design decisions (baseline HEAD 58eb196baa)

## Dependency boundaries (link edges)

- `sicnu_data` (src/data): DataManager (asset authority), execution_fingerprint,
  artifact_store (streaming `artifactContentDigest` SHA-256, artifact_store.cpp:120),
  artifact_object_pool, governance/* (GovernanceStore SQLite WAL, WorkspaceService …).
- `sicnu_workflow` (src/workflow): links jsoncpp + sicnu_operators only — must NOT
  gain a data dependency (cycle avoidance). Coordinator `.cpp` compiles into
  `sicnu_task_center` (src/processing/CMakeLists.txt:249) which PUBLIC-links
  `Sicnu::data` → coordinator can use artifactContentDigest.
- `sicnu_task_center`: TaskCenter + execution cache (ExecutionResultCache in
  execution_fingerprint.{h,cpp}) + WorkflowRunCoordinator.
- `sicnu_runtime` (Qt-free): worker_protocol.h (wire v1), observability
  (ExecutionTelemetry), chunk/, gpu/. Worker binary: src/cli/sicnu_worker_main.cpp.
  Host: src/processing/framework/local_worker_host.{h,cpp} — one-shot QProcess per
  call, zero production callers (seam only).

## Identity model (current, audited)

- DataManager AssetId + AssetRevision: revisions advance on registerSource(reuse w/
  structure change or notifyUpdateOnReuse), relocate, commitEdit,
  notifyExternalContentChange (zero production callers today).
- Execution fingerprint V2 (contract v2): SHA-256 over canonical (algorithmId,
  impl-identity = SHA256(schema+contract), RFC-8785 params, sorted TaggedDerivationInputs).
  Registered inputs → (assetId, revision); chained inputs → producerFingerprint;
  `lazyContentDigest` fully plumbed, never produced.
- Cache validation: post-hoc stat only — `declaredOutputStillValid` (size+mtime over
  artifactSizes/Msecs + inputSizes/Msecs); inputSizes recorded ONLY for ChainedEdge
  (task_center.cpp:2865-2873). Lookup→serve TOCTOU closed by SourceExpectation re-stat.
- Governance mirror: GovernedAsset rows carry contentFingerprint/size/mtime/verifiedMs
  (noteAssetVerified bumps governance revision on digest change — zero callers);
  MetadataPipeline drift detector flags stale in mirror only, `start()` unwired.
- StepPlan checkpoint: status/taskId/resultPayload/outputLayerPath persisted;
  `fingerprint`/`cacheHit`/`cachedOutput*` fields vestigial (round-trip, never written).
  Resume gate = path exists && size>0 (workflow_run_coordinator.cpp:519-538).
- Run states: WorkflowRunState 10 values; Interrupted is terminal-but-resumable;
  finalizeRunLocked rolls up Completed/Failed/Canceled; per-transition checkpoints.
- ArtifactStore: content-addressed data/<digest>; ArtifactRegistration has
  producerFingerprint/contentDigest/size/mtime; pool (artifact_object_pool) reaps by
  fingerprint refcount only (artifact_refs never attached → shared objects deleted);
  totalObjectBytes() full-tree-walks after every storeExecution.

## Design decisions for the epic (ADR-worthy)

1. **Governed document authority** (A): the project file's `<workspace>` block is the
   authority for governed state; the SQLite store is a derived, rebuildable index.
   Read v3 always caches the parsed document (even with store closed/unavailable);
   save prefers live store, falls back to cache, never silently downgrades a
   v3-seen project (refuse save = fail-conservative). Cache + v3-seen cleared at
   project transition (clearProject) → no cross-project bleed.
2. **Snapshot consistency** (A): `GovernanceStore::checkpointForBackup()` runs
   `PRAGMA wal_checkpoint(TRUNCATE)` under the impl mutex; snapshot refuses (with
   diagnostic) when checkpoint fails; sidecars copied only if present after
   successful checkpoint (defensive).
3. **Restore diagnostics** (A): fromProjectJson collects every upsert Result;
   aggregate `workspace.restore_failed` diagnostic with per-entity counts; read-only
   store surfaced like closed store; `busy_timeout` applied to every connection.
4. **Cache external mutation** (D): (a) registered inputs recorded into
   inputSizes/inputMsecs at store time (fail-closed stat); (b) content digests for
   registered inputs below a size threshold enter the fingerprint via the existing
   `lazyContentDigest` seam (contract v2 unchanged) — same-size/same-mtime rewrites
   change the digest → miss; (c) DataManager grows a bounded local-file watcher that
   calls `notifyExternalContentChange` (revision bump → assetChanged → governance
   mirror + fingerprint change). Verification states: stat always, digest when
   affordable.
5. **Resume identity** (E): StepPlan gains `outputSizeBytes`/`outputMtimeMs`/
   `outputDigest` (new JSON keys; fromJson ignores unknown fields → forward/backward
   compatible files). Coordinator stamps at Completed fold (stat always; digest when
   size ≤ threshold). Resume gate: verify stat; verify digest when recorded and
   affordable; mismatch → re-execute step; missing identity (legacy checkpoint) →
   re-execute (fail-conservative; documented in crash-resume contract).
6. **Truthful runs** (F): WorkflowRunCoordinator gains an injected run-state observer
   seam (no hard workflow→governance dependency); app binds it to
   WorkspaceService::recordRun (Running at start; terminal at finalize incl.
   Interrupted). Mirror writers stop fabricating "Completed": they upsert run rows
   only when absent, with state "Unknown"; never overwrite observer-recorded states.
   orphanResults treats run-missing OR Failed/Canceled/Interrupted as orphan.
7. **Store hardening** (B): single-statement writers return Result and check
   step/COMMIT (ROLLBACK + cascade-safe); alias inserts collision-checked like
   canonical paths; summary counts via COUNT(*); prepare-once/reuse + batch
   transactions in bulk read paths.
8. **CAS reference safety** (C): object eviction deletes data/<digest> only when no
   live fingerprint/record still references the digest (digest-aware accounting +
   incremental storage stats instead of per-write full-tree walks); asset removal
   deletes only OUTGOING lineage edges; ReproBundleExporter mutates a local options
   copy.
9. **Worker runtime** (H): bounded warm worker pool on worker_protocol v1 (reusable
   processes, ready-handshake health, lifetime cap, crash → respawn, per-job
   timeout/cancel escalation, telemetry events), integrated as a JobEngine JobExecutor
   seam — no second scheduler.
10. **Remote plane** (I): validator semantics (ETag/Last-Modified/length) recorded at
    registration/verify time through the existing RemoteSourceValidator seam; dedupe
    the two GDAL config helpers; offline ⇒ never-stale preserved; no UI-thread
    network I/O.

## Explicit non-goals

No second job scheduler; no new wire protocol version; no GUI redesign; no
fingerprint contract v3; no persistent byte cache for remote rasters (future).
