# ADR 0177: Workflow Durability — Crash Windows, Lock Semantics, Cache-Seeding Gate

## Context

The workflow engine's durability story was assembled incrementally across
#697/#668 (checkpoints, recovery, GC), #727 (cross-process run ownership via
flock), Track 13 (lineage envelope, checkpoint election), #750/#731
(completion identity), 8.0 WP-E (operator implementation identity), #931/#944
(IO off the coordinator mutex), #1078/#1078a (terminal wedges), #1158/#1152
(cancel races), and #1323 (atomic checkpoint publish). Each landed with
in-process tests; what no lane pinned end-to-end was the actual death of the
owning PROCESS at each lifecycle point — every prior "crash" was simulated
inside the dying process's own address space. The durability claims that
depend on kernel behavior (flock release on ANY process death, atomic
tmp→fsync→rename publish) were therefore trusted, not verified.

Track 10 (workflow-durability R4) closed that gap with a real-process crash
matrix: a `workflow_crash_helper` binary runs the production
coordinator/TaskCenter/JobEngine stack, a parent test SIGKILLs it at each
injection point, and every assertion reconciles against the on-disk
artifacts (raw checkpoint JSON), never against the recovery code's own view.

This ADR records the semantics the matrix either pinned for the first time
or found already correct — so the next hardening track argues with the ADR,
not with folklore.

## Decision

### 1. The journal is the checkpoint file; the commit boundary is the atomic publish

Per-transition persistence (`WorkflowRunCoordinator` folds →
`WorkflowCheckpointManager::saveCheckpoint`) makes `checkpoint_<runId>.json`
the run's journal. A step is *committed* iff its plan status in the last
intact checkpoint is `Completed`. Death windows behave as follows (all
verified by real SIGKILL in `test_workflow_crash_recovery_r4`):

* **tmp written, no rename** (death mid-save): the previous checkpoint stays
  byte-identical; the tmp is orphaned; the recovery pass sweeps tmp files of
  runs whose lock has no live holder and never touches a live writer's
  in-flight tmp.
* **death before a step's fold**: its output file may exist on disk, but the
  journal did not commit it — resume MUST re-execute the step (the stale
  bytes are never served).
* **death after a step's commit**: resume serves the committed step subject
  to the #750 identity gate (stat + content digest) and the 8.0 WP-E
  operator-identity gate, both fail-closed to re-execution.
* **death after the last commit, before the finalize persist**: the
  all-committed picture reconciles to Interrupted and finalizes from its
  committed set without re-executing anything (#1078a path).

### 2. Lock semantics (unchanged, now pinned)

flock(2) per open file description; a second descriptor — same process or
not — observes `HeldByLiveOwner`. Liveness is ALWAYS the lock primitive;
owner metadata (pid/hostname/token) is diagnostics-only and may be garbage,
truncated, or empty without affecting ownership. `probeOwner` on Q_OS_UNIX
returns only `NoHolder`/`LiveOwner`; `Unknown` is reachable only on the
QLockFile (non-Unix) path. `release()` is idempotent and destructor-backed.
Lock files are never unlinked.

### 3. Cancel is sticky through the state machine

`cancelRun` persists `Cancelling` BEFORE propagating to TaskCenter, so a
death during propagation leaves either the mid-flight picture (reconciled to
`Interrupted`, stuck steps reset `Pending`, resumable) or the already-terminal
`Canceled` picture (inert for recovery adoption, resumable explicitly). The
guarded transition table refuses transitions OUT of terminal states, so a
late `cancelRun` can never resurrect a `Completed` run. Resume of a canceled
lineage serves the committed prefix and re-executes canceled/unfinished
steps under the SAME runId.

### 4. The execution cache seeds ONLY on real completion; invalidation travels through content lineage

Pinned against ADR 0125 §3's contract:

* A canceled (or failed) step never seeds `ExecutionResultCache`, even when
  it was dispatched and executing when the cancel landed; a later real
  completion of the same step is what seeds it.
* A downstream entry is keyed on its inputs' `TaggedDerivationInput{assetId @
  revision}` (plus lazy content digest). An upstream re-run strands the
  downstream entry by MOVING THE KEY (new revision / new digest), not by any
  per-node invalidation bookkeeping; `invalidate(fp)` is targeted and must
  not over-reach into sibling lineages. A same-size, restored-mtime rewrite
  must still move the key via the content digest (#749).
* Resume + cache compose: completed steps served from the journal and steps
  served from the execution cache both count as committed in the
  checkpoint; the checkpoint's committed set and the TaskCenter completed-task
  view agree for one run (the two-view reconciliation resume depends on).

## Consequences

Crash/lock/cancel/cache behavior is now enforced by process-death tests, not
only by in-process unit lanes. New durability work must extend
`workflow_crash_helper` (or reuse its modes) instead of simulating deaths
in-process — an in-process "crash" cannot witness the kernel releasing the
flock, which is the foundation of the whole ownership contract.

## Alternatives Considered

* Extending ADR 0125 instead — rejected: 0125 scopes the temporal workspace;
  these semantics belong to the workflow engine and outlive it.
* A fork-based injector inside the test process — rejected: kill/inherit
  semantics of fork differ from the production "independent process dies"
  model, and QLockFile-parity would be lost; the helper-binary pattern
  (`exprs_ep_helper` tradition) already exists and is cross-platform.
