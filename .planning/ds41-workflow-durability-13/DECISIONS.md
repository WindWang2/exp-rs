# DECISIONS — ds41-workflow-durability-13

Baseline: `79adfe78a16b9419eef180cf9e6e5739658621a2` (origin/master at worktree creation).

## D1 — Kernel of record: same two stacks as the predecessor
D17/IR2 (`PipelineRunCoordinator`, Qt/JSON) owns run-level durability oracles; Engine-2.0
(`WorkflowRun`/`WorkflowCheckpointManager`) owns the on-disk checkpoint/election layer. No new
stack, no merge of the two.

## D2 — Artifact identity: tag-driven scheme with `sha256full`, `Auto` default
`computeArtifactFingerprint` becomes mode-driven and self-describing:
- `sha256fl:<hex>` — unchanged framing `[8B LE size][first ≤1MiB][last ≤1MiB]`; whole file when
  ≤2 MiB. Kept for compatibility and for small artifacts.
- `sha256full:<hex>` — same framing prefix, then the WHOLE file streamed in 1 MiB chunks
  (bounded memory). Polls a cancel flag once per chunk.
- Mode enum `ArtifactIdentityMode { Fast, Full, Auto }`; **Auto is the default**: full hashing
  exactly when the artifact is larger than the fast window (2 MiB) — i.e. precisely when
  `sha256fl` has a blind spot; small artifacts keep the cheap path. The tag recorded per node
  selects the verification algorithm on resume, so mixed and legacy checkpoints verify
  correctly. An unrecognized tag fails closed.
- Why not Merkle: a chunked whole-file SHA-256 is the minimal provable scheme; a Merkle tree
  adds structure (and a second format) without closing any additional hole for this use.

## D3 — Cancel-aware hashing + flag-first cancel
The full hash runs on the coordinator affinity thread (where `onNodeFinished` already
fingerprints) but streams in 1 MiB chunks and polls the existing `cancelRequested` atomic.
`requestCancel` is restructured into two phases: (1) lock-free atomic set from ANY thread, so a
long hash aborts within one chunk; (2) the status-mutating body marshalled to the affinity
thread as today. Phase 2 therefore never waits for a whole-file hash. A cancel observed mid-hash
marks the node Cancelled (the run did not produce it), distinct from `ir2.artifact_unverifiable`
for genuinely unreadable files.

## D4 — Lineage envelope: explicit `attempt` (+ ghost `resumeOf`), version 2, legacy 1 accepted
Engine-2.0 `WorkflowRun` gains `attempt` (1-based) and, for the temporary resume submission,
`resumeOf` (the original runId). `toJson` writes them; serialization version becomes **2**;
`fromJson` accepts version 1 (legacy: `attempt=1`, no `resumeOf`) and refuses anything else with
a named error. This separates user identity (runId, unchanged) from system attempt lineage
(envelope fields) — no more encoding lineage in the runId string.

## D5 — Election by declared lineage; legacy suffix rule only for legacy files
`electCheckpoints` groups a checkpoint by CONTENT when the file parses:
- version ≥2 with `resumeOf=X` → group X (the ghost joins its original regardless of filename);
- version ≥2 without `resumeOf` → group its own runId (standalone; a user-named `foo_resume` is
  never merged into `foo`);
- unparseable / legacy version-1 → the historical filename-suffix rule (migration recognizes
  legacy `_resume` ghosts; the existing fault-injection fixture — bare `{}` files — keeps its
  exact semantics and still quarantines one).
Grouping never reads a v2 file's filename for lineage, so no user-chosen id can be mis-grouped.

## D6 — D17 provenance attempt layout: path segment, not suffix
`provenance_<runId>.json` (attempt 1) is unchanged. Attempt N>1 moves to
`provenance_<runId>.attempt<N>.json` → **`attempt-<N>/provenance_<runId>.json`** inside the run
directory. Because a valid runId cannot contain `/`, the user identity (filename) and the system
lineage (path segment) can never collide — unlike any single-segment concatenation, where the
runId charset swallows the delimiter. The record's JSON envelope already carries the attempt
semantics; only the layout changes. `provenancePath()` returns the new path.

## D7 — Affinity: one pattern for every public entry point
The existing accessor pattern (marshal onto the affinity thread with
`Qt::BlockingQueuedConnection`, run inline when already there) is applied to ALL public
mutators — no mixed patterns. The affinity thread never blocks on a foreign thread (only the
destroying thread may block on it), so there is no deadlock cycle. The destructor marshals a
shutdown body (drain pool, set the shutdown flag, `removePostedEvents`) so a foreign-thread
destruction cannot race `onNodeFinished`; destroying on the affinity thread stays the documented
fast path (inline).

## D8 — Build/test strategy: light lanes only
No qgis_core/qgis_gui/task_center rebuild in this track. New Engine-2.0 coverage goes into a
new light target (`test_workflow_durability_13`) that compiles the checkpoint/run/lock sources
directly (established `sicnu_add_d17_test` pattern). The two small `workflow_run_coordinator.cpp`
seam edits are verified by a single-TU syntax/type check plus review; the existing heavy
Engine-2.0 suites remain the integration gate for CI.

## D9 — Fault points for publish boundaries
D17 checkpoint/provenance publishes gain `SICNU_FAULT_POINT` seams on their REAL failure
branches (same discipline as `workflow_checkpoint.write`/`.publish`): tests arm the fault to
simulate a crash between tmp-write and rename and assert old-file integrity + no residue.
