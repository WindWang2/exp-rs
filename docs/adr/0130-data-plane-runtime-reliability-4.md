# ADR 0130: Data Plane, Runtime, Governance & Reproducibility Reliability 4.0

- Status: Accepted (2026-09-06)
- Scope: `src/data`, `src/data/governance`, `src/workflow`, `src/processing/framework`, `src/jobs`, `src/runtime`, governance/cache/workflow tests
- Supersedes parts of: ADR 0129 (governance/snapshot/cache behaviors it introduced unhardened)
- Issue cluster: #746 (P1), #749, #750, #751, #752, #753, #754, #758

## Context

Platform 3.0 (ADR 0129) shipped the governance store, project format v3,
snapshots, the artifact/object pool and the execution cache. A review cluster
(#746–#758) showed the foundation was not yet fail-conservative: a corrupt
governance DB silently downgraded a v3 project to v1 (permanent governed-state
loss), snapshots copied a hot WAL with the checkpoint stubbed out, restore
discarded every store failure, cache/resume could serve foreign bytes, run
states were fabricated, and the store had unchecked writes plus page-size
count claims.

## Decisions

1. **Document authority (governance).** The project file's `<workspace>` block
   is the authority for governed state; the SQLite store is a derived,
   rebuildable index. A v3 read always caches the parsed document (even with
   the store unavailable); a save prefers a live integrity-probed store, falls
   back to the cache with a `workspace.stale_document_repersisted` warning,
   and — defense-in-depth for the state where neither live document nor cache
   exists — fails with `workspace.downgrade_refused` instead of silently
   rewriting a v3 project as v1. Because QGIS's writeProject signal cannot
   veto the file write, that refusal surfaces the loss loudly; the practical
   prevention is the cache-on-read rule itself. Cache + v3-seen marks are
   cleared on project transitions — governed state never bleeds across
   projects. `WorkspaceService::storeIntegrityOk()` (open + writable +
   `PRAGMA quick_check`) gates serialization.
2. **Snapshot consistency.** `GovernanceStore::checkpointForBackup()` steps
   `PRAGMA wal_checkpoint(TRUNCATE)` under the connection mutex and requires
   busy == 0 in the result row (sqlite3_exec would discard that row and hide
   a blocked checkpoint); a snapshot refuses (error naming the checkpoint)
   unless the TRUNCATE completed, and only the main DB file is copied.
   Snapshots therefore require a writable store; the window between
   checkpoint and copy is covered by the store mutex on the snapshotting
   side. `busy_timeout` applies to every connection, not only schema
   creation.
3. **Restore is explicit.** `fromProjectJson` attempts every upsert, counts
   failures, and aggregates `workspace.restore_failed`; read-only stores
   surface `workspace.store_read_only`. Forward tolerance stays explicit.
4. **Store write integrity.** Every `COMMIT` is checked with rollback-on-
   failure; single-statement writers verify `step()` and return `Result`;
   alias ownership is collision-checked symmetrically (existing owner kept);
   summaries use real `COUNT(*)`; bulk read paths reuse prepare-once
   statements.
5. **CAS reference safety.** Pool eviction deletes `data/<digest>` only when
   no live record references the digest (`liveByContentDigest`); storage
   accounting is incremental (no tree walk on the under-budget store path);
   asset removal deletes only the removed asset's own provenance edges;
   bundle export mutates a local options copy.
6. **Cache correctness under external mutation.** Registered (non-chained)
   inputs are stat-bound into the cache entry at store time and re-validated
   at lookup; small registered inputs add a content digest to the fingerprint
   through the existing contract-v2 `lazyContentDigest` seam
   (`SICNU_CACHE_INPUT_DIGEST_MAX_MB`, default 64 MiB, 0 disables); a bounded
   `DataManager` watcher (`SICNU_DATA_WATCH_LIMIT`, default 4096 paths)
   advances revisions via `notifyExternalContentChange` on out-of-band
   rewrites.
7. **Crash-resume identity.** `StepPlan` checkpoints carry a completion
   identity (size + mtime always; SHA-256 digest within
   `SICNU_RESUME_DIGEST_MAX_MB`, default 256 MiB). The resume gate re-verifies;
   mismatched or unverifiable (legacy) outputs re-execute the step. Resume
   stays semantically equivalent to fresh execution (#731).
8. **Truthful run states.** `WorkflowRunCoordinator` exposes a run-state
   observer (Running / Completed / Failed / Canceled / Interrupted);
   `ProjectContext` binds it to `WorkspaceService::recordRun`. The governance
   mirror never fabricates states (existing rows keep their recorded state;
   new rows start `Unknown`); orphan results include those anchored only by
   failed/canceled/interrupted runs.
9. **Bounded warm worker pool (opt-in, seam as shipped).** `LocalWorkerPool`
   reuses the worker-protocol v1 architecture: handshake health checks,
   per-job timeout, cancel escalation, crash → typed error + replacement for
   future jobs, lifetime recycling, telemetry, safe shutdown. It is an
   executor-side resource, not a second scheduler. As shipped it is a fully
   tested, NOT-YET-WIRED seam — no production dispatch path calls it yet;
   wiring an opt-in executor into TaskCenter/JobEngine is the tracked
   follow-up. Thread contract: pool calls must come from the starting thread
   (QProcess affinity is enforced at run()).

## Consequences

- Every claim above is pinned by a test or an explicit contract entry in
  `docs/architecture/FAULT_MATRIX_4.md` (24 fault rows). Known seams:
  the MCP surface reads run state through the authoritative
  `setWorkflowRunsProvider` (not the governance runs mirror), and its
  separately-opened store therefore records `Unknown` run rows — accepted,
  documented divergence; the integrity probe is throttled (2 s positive
  window) so saves at 100k scale do not pay a full `quick_check` each time;
  the corrupt-while-open detection is best-effort (SQLite's page cache may
  serve a small corrupted DB, in which case the session keeps serializing
  the cached-page content).
- Legacy checkpoints (pre-identity) re-execute on resume — an intentional,
  documented fail-conservative migration cost.
- The 100k scale contract holds after hardening (SCALE_BASELINE:
  ingest ~1.1 s, paged query 76 ms, facet 5–22 ms, 1000 lookups 39 ms,
  10k bulk tag 15 ms, depth-64 lineage <1 ms).
- A concrete HTTP validator for remote sources remains future work: `src/data`
  is network-free by design, so ETag/Last-Modified validation stays a
  test-covered seam until an app-layer implementation lands.
