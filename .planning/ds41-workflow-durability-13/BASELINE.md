# BASELINE — ds41-workflow-durability-13

Captured 2026-09-21 ~03:50 (+08:00), after `git fetch origin --prune`.

## Master snapshot
- `origin/master` = `79adfe78a16b9419eef180cf9e6e5739658621a2` ("Merge pull request #1134 from WindWang2/agent/flash-plugin-sdk-12", 2026-09-20 21:39 +08:00).
- Worktree `exp-rs-worktrees/ds41-workflow-durability-13`, branch `agent/ds41-workflow-durability-13`, HEAD == base == `79adfe78a` (created via `scripts/dev/new_worktree.py`).
- Predecessor of this track: PR #1132 (flash-workflow-engine-12, IR2 durable schema versioning / subflows / checkpoint provenance) — MERGED. Its DECISIONS D3 names the exact limitations this track hardens.

## Open PRs (3) — none in the workflow/durability domain
| PR | Branch | Domain | Overlap with this track |
|----|--------|--------|--------------------------|
| 1137 | agent/ds41-geospatial-maintenance-13 | retry admission, Stat TTL parity, orphan deletion | none (src/geospatial) |
| 1136 | agent/ds41-offline-labs-13 | offline labs registry, lab identity, CSV safety | none (src/app labs) |
| 1135 | agent/flash-temporal-phenology-12 | temporal phenology analytics | none (src/operators temporal) |

Issues open: 0.

## Known limitations on master (verified by reading the code at 79adfe78a)

1. **`sha256fl` blind spot** — `src/workflow/pipeline_run_coordinator.cpp:83` `computeArtifactFingerprint`
   hashes `[8B LE size][first ≤1MiB][last ≤1MiB]`. A mid-file-only rewrite of a >2 MiB artifact that
   preserves size and mtime evades detection (the predecessor's DECISIONS D3 states this openly).
   Existing tamper test (`tests/test_workflow_checkpoint_cache.cpp:783`) only exercises small
   (<2 MiB) synthetic artifacts, which `sha256fl` covers whole — the gap is untested.
2. **`_resume` election collision** — `src/workflow/workflow_checkpoint.cpp:238` `electCheckpoints`
   groups `checkpoint_<runId>.json` files by stripping a trailing `_resume` from the FILENAME.
   A user-named run `foo_resume` (`isValidRunId` accepts it; `createFromDefinition` accepts
   caller-provided ids) is grouped with unrelated run `foo`, and one of the two is quarantined as
   `.orphaned` — a legitimate run's checkpoint is destroyed by another run's election. Conversely a
   system resume ghost and a user run with the same name share one filename with nothing to
   disambiguate them. There is no envelope field separating user identity from system lineage:
   `WorkflowRun::toJson` (workflow_run.cpp:566) carries no attempt/lineage data, and the Engine-2.0
   resume path encodes lineage only as `def.id + "_resume"` (workflow_run_coordinator.cpp:1065).
3. **Adjacent hole found during baseline read** — the resume ghost created by
   `startTrackedPipeline(remaining)` receives a GENERATED runId (`run-<ms>-<seq>-<hex>`, no
   `_resume` suffix), so `electCheckpoints` can never group it with its original: a crash between
   the ghost's first persist and the post-swap `QFile::remove` leaves a duplicate Interrupted
   checkpoint that recovery resurrects as a second run of the same remaining steps.
4. **D17 provenance filename collision** — `finalizeIfDone` names attempt N>1 provenance
   `provenance_<runId>.attempt<N>.json` where runId is user-controlled (it is whatever the resumed
   checkpoint file was named). A user run `foo.attempt2` at attempt 1 and run `foo` at attempt 2
   derive the SAME filename; one record silently overwrites the other. Any concatenation of a
   user-controlled id with a system token in one path segment is ambiguous because the runId
   charset `[A-Za-z0-9._-]` contains the delimiters.
5. **Coordinator mutator affinity holes** — `PipelineRunCoordinator` marshals its ACCESSORS
   (`checkpointPath`, `provenancePath`, `isRunning`, `hasCompleted`, `getAllStatuses`) and
   `requestCancel` onto its affinity thread, but the MUTATORS `startRun`,
   `resumeFromCheckpoint`, `setExecutor`, `setMaxParallelism` mutate `m_state` (def, statuses,
   pool, paths) directly from the caller's thread. A foreign-thread call races `onNodeFinished`.
   The destructor also runs `pool.waitForDone()` on whatever thread destroys the object.
   `requestCancel` marshals its whole body with `Qt::BlockingQueuedConnection`, so a canceller
   blocks for as long as the affinity thread is busy — unbounded once a full-file hash exists.

## Historical branches
`stale_branches.py`-style audit of `agent/*` / `track/*` / `fix/*` remotes: all recent workflow work
landed via #1132; `agent/flash-workflow-integrity` (the pre-#1132 integrity branch) is fully
superseded per the predecessor's BASELINE.md. No unmerged workflow increment to reuse.

## Build environment (this session)
Linux, 40 cores / 62 GB, load ~23 from sibling sessions (sar-radiometry-13, flash-data-scale-13,
flash-spectral-intelligence-13, model-runtime-13 …). Discipline: `-j2` max, targeted builds only.
Heavy chain (qgis_core/qgis_gui via sicnu_task_center) is NOT rebuilt in this track; all test
targets used are the light D17-style family (Catch2 + Qt6 + jsoncpp, sources compiled in).
