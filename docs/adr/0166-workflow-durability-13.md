# ADR 0166 — Workflow Durability 13.0: strong artifact identity, lineage envelope, affinity discipline

## Status

Accepted (ds41-workflow-durability-13)

## Context

The durable-IR2 work (flash-workflow-engine-12, ADR 0162 lineage) merged schema
versioning, subflow composition and checkpoint provenance, and documented four
known limitations it deliberately left open. Master (`79adfe78a`) still has them:

1. **`sha256fl` blind spot.** Node artifact identity is SHA-256 over
   `[8B LE size][first ≤1 MiB][last ≤1 MiB]`. For an artifact larger than 2 MiB a
   mid-file-only rewrite that preserves size and mtime (trivially forgeable —
   `restoreMtime` in the existing tests) evades detection, and a tampered product
   is then served as a CacheHit on resume.
2. **`_resume` election collision.** `WorkflowCheckpointManager::electCheckpoints`
   groups `checkpoint_<runId>.json` files by stripping a trailing `_resume` from the
   FILENAME. A user-named run `foo_resume` (`isValidRunId` accepts it;
   `createFromDefinition` accepts caller-provided ids) is grouped with unrelated
   run `foo` and one of the two legitimate runs is quarantined as `.orphaned`.
   The Engine-2.0 resume path encodes lineage only as `def.id + "_resume"`, and
   `WorkflowRun`'s payload carries no lineage field at all — so nothing
   distinguishes a user identity from a system attempt lineage. The resume ghost
   created by `startTrackedPipeline` even receives a GENERATED runId, which the
   filename rule can never group with its original: a crash between the ghost's
   first persist and the post-swap delete leaves a duplicate Interrupted
   checkpoint that recovery resurrects as a second execution of the same steps.
3. **Provenance filename collision (D17).** Attempt N>1 provenance is named
   `provenance_<runId>.attempt<N>.json` with a user-controlled runId; run
   `foo.attempt2` at attempt 1 and run `foo` at attempt 2 derive the same
   filename. Any single-segment concatenation of a user id with a system token
   is ambiguous, because the runId charset `[A-Za-z0-9._-]` contains the
   delimiters.
4. **Coordinator affinity holes.** `PipelineRunCoordinator` marshals its
   accessors and `requestCancel` onto its affinity thread, but the mutators
   `startRun`, `resumeFromCheckpoint`, `setExecutor`, `setMaxParallelism` mutate
   `m_state` from the caller's thread and race `onNodeFinished`; the destructor
   drains the pool on whatever thread destroys the object; and `requestCancel`
   marshals its whole body, so a canceller blocks for as long as the affinity
   thread is busy — unbounded once a whole-file hash exists.

## Decision

### D1 — Tag-driven artifact identity with `sha256full`, `Auto` default

`computeArtifactFingerprint` becomes mode-driven and self-describing. `sha256fl`
keeps its exact legacy framing and tag. `sha256full` uses the same framing prefix
and then streams the WHOLE file in 1 MiB chunks (bounded memory) while polling a
cancel flag once per chunk. `ArtifactIdentityMode { Fast, Full, Auto }` selects
what a NEW success records; `Auto` (default) chooses `Full` exactly when the
artifact exceeds the fast window — precisely when `sha256fl` has a blind spot.
Resume verification is tag-driven: the recorded tag selects the algorithm, so
legacy `sha256fl:` records verify with the fast scheme and `sha256full:` records
with the whole-file hash; mixed checkpoints need no migration and an unknown tag
fails closed. A chunked Merkle tree was rejected as a larger format that closes
no additional hole.

### D2 — Two-phase cancel

`requestCancel` phase 1 sets the existing `cancelRequested` atomic from ANY
thread (lock-free), so a whole-file hash in flight aborts within one chunk;
phase 2 keeps the status-mutating body marshalled onto the affinity thread. A
cancel observed mid-hash marks the node `Cancelled` — the run did not produce it
— distinct from `ir2.artifact_unverifiable` for genuinely unreadable files. The
resume verification loop polls the same flag between nodes and aborts the resume
before dispatch.

### D3 — One marshalling pattern for every public entry point

The accessors' existing pattern (marshal onto the affinity thread with
`Qt::BlockingQueuedConnection`, run inline when already there) is applied to all
mutators through one shared helper. The affinity thread never blocks on a
foreign thread, so no call can deadlock against it; a foreign caller may block
until the owner is idle, bounded by the owner's current unit of work. The
destructor trips the cancel flag from any thread, then marshals a drain (pool
clear + `waitForDone` + `removePostedEvents`) behind a `shuttingDown` guard, so a
foreign-thread destruction cannot race `onNodeFinished`.

### D4 — Explicit lineage envelope; election by declared lineage

`WorkflowRun` gains `attempt` (1-based, persisted) and `resumeOf` (names the run
a temporary resume submission continues). Serialization version becomes 2;
version-1 payloads still load (migration: `attempt=1`, no `resumeOf`) and any
other version is refused by name. A corrupt envelope (out-of-range or
fractional attempt, unsafe `resumeOf`) is refused. `electCheckpoints` groups by
the DECLARED lineage: a version-2+ file declaring `resumeOf: X` groups under X
whatever its filename; a version-2+ file without `resumeOf` groups under its own
runId (standalone — a user-named `foo_resume` is never merged into `foo`); a
non-recovery-candidate payload is inert and standalone; legacy and corrupt
payloads keep the historical filename-suffix rule, which is the migration path
for pre-envelope ghosts. Within a group the COMPLETE lineage wins over a resume
ghost (the ghost is derivable from the original, which also carries every
earlier pass's completed plans); recency decides only among files of the same
kind. `startTrackedPipeline` accepts the lineage and stamps it before the first
persist, and the resume path advances the original run's attempt counter.

### D5 — Lineage in a path segment, identity in the filename

D17 provenance for attempt 1 stays `provenance_<runId>.json`; attempt N>1 moves
to `attempt-<N>/provenance_<runId>.json` inside the run directory. Because a
valid runId cannot contain `/`, the user identity (filename) and the system
lineage (path segment) can never collide.

### D6 — Publish-boundary fault points

The D17 atomic writer gains an optional fault name; the checkpoint and
provenance publishes pass theirs, routing through the REAL rename-failure
branch (tmp removed, nothing promoted). Tests arm them to simulate a crash
between tmp-write and rename and assert old-file integrity plus no residue.

## Consequences

- A >2 MiB mid-file tamper is caught by default; operators who need the old
  cost profile can select `Fast` per coordinator, with the documented blind
  spot asserted in the suite so the trade-off stays visible.
- User-named `*_resume` runs survive election; crash-leftover resume ghosts are
  grouped with their originals regardless of their generated filenames, and the
  original (complete lineage) is the one kept.
- All public coordinator entry points share one threading contract; a
  foreign-thread cancel or destruction cannot deadlock or race the owner.
- Engine-2.0 checkpoints written by this build carry version 2; readers accept
  version 1. Older builds reject version 2 by name (fail closed) — no silent
  reinterpretation in either direction.
- New light test lane `test_workflow_durability_13` covers the Engine-2.0
  envelope/election/fuzz without the qgis/task_center chain.
