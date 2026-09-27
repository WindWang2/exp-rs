# PLAN — Track 10: Workflow Durability R4

## Oracle (goal-loop target, from charter §6 + measured reality)

1. `ctest -R "workflow|durability|run_lock|lowering" -j1` in a fresh build dir, **twice
   consecutively green** relative to the Phase-0 baseline red set (zero NEW failures);
   **≥5 real-process crash injection points** and **≥15 lock/cancel/cache cases** countable in
   the output.
2. `git rev-list --count origin/master..HEAD` ≥ 10 atomic commits, each independently
   compilable.
3. ≥ 12 files touched, all inside the whitelist; the crash injector reuses the existing
   primitives (fault registry, atomic_fs staging, WorkflowRunLock, checkpoint save/load, the
   `helper_external_process` QProcess pattern) — no second mechanism.
4. `.goal-loop-ledger.md` complete (per-round: change → verification → result → round/cumulative
   tokens); `.planning/workflow-durability-r4/` holds BASELINE/PLAN/DECISIONS/EVIDENCE/
   REVIEW_LOG.
5. ≤ 3 subagents total; never above `-j2` (drop to `-j1` if RSS > 70%, recorded); no CI waits;
   no out-of-whitelist touches.
6. PR open with the mandated body; title honest.
7. Zero new directions in the diff.

## Work packages → concrete seams (measured)

- **WP-A (injector + IP-1/IP-2)**
  - `tests/workflow_crash_helper.cpp` — one static-linked helper binary (links the same set as
    `test_workflow_recovery`), modes:
    - `hold-lock <dir> <runId>` → `WorkflowRunLock::tryAcquire`, print `LOCKED`, spin (kill target).
    - `run-block-at <dir> <runIdPrefix> <blockStep> <completedBefore>` → registers JobEngine
      executors; each completed step writes a REAL output file; the blocking executor prints
      `STEP <n> RUNNING` and sleeps until killed. Runs a REAL `startTrackedPipeline` so the
      coordinator folds + persists checkpoints exactly as production.
    - `run-exit-at <dir> <runIdPrefix> <exitStep>` → the named executor writes its output then
      `_exit(70)` (hard death before the step's fold can commit).
  - `tests/workflow_crash_injector.{h,cpp}` — parent-side fixture: spawn/barrier/SIGKILL/wait,
    read-back of on-disk truth (`WorkflowCheckpointManager::loadCheckpoint` + raw JSON), lock
    probe. RAII kills stragglers.
  - `tests/test_workflow_crash_recovery_r4.cpp` — IP-1 (kill while holding lock ⇒ kernel
    releases: tryAcquire succeeds + probeOwner=NoHolder + no half state), IP-2 (kill mid-node ⇒
    committed set == on-disk journal set; resume does not re-execute committed; run completes).
- **WP-B (IP-3/4/5 + lock semantics)**
  - IP-3 torn checkpoint: helper `torn-save` mode staging a `.tmp` via
    `sicnu::geo::atomic_fs::stagedPathFor` + partial bytes, killed pre-rename ⇒ previous
    checkpoint intact, recovery sweeps the tmp; PLUS the deterministic failure-branch side by
    arming `workflow_checkpoint.publish` (existing fault point — reuse).
  - IP-4 commit windows: pre-commit (death before fold ⇒ step re-executes, half-state Running
    resets to Pending); post-commit (death after step committed ⇒ served, not replayed; the
    all-committed variant finalizes without execution).
  - IP-5 cancel race: cancel against a blocking executor; kill during propagation; assert
    sticky-Cancelled/Cancelling semantics, no resurrection of terminal runs, completed steps
    stay committed.
  - Lock-semantics cases (new file `test_workflow_run_lock_r4.cpp`, durability_13-style light
    lane): release idempotence (double release), tryAcquire-after-release re-acquire,
    cross-process HeldByLiveOwner with pid metadata, probeOwner NoHolder/LiveOwner (Unix
    documented states), lock file listing not confused with checkpoints, recovery skips
    live-owner runs and sweeps tmp only for unowned runs.
- **WP-C (cancel propagation + cache coherence)** — extends `test_workflow_cancel.cpp`,
  `test_workflow_cache_e2e.cpp`, `test_workflow_incremental_cache.cpp` traditions:
  - cancel reaches a running cooperative operator (RSOperatorContext::throwIfCancelled) —
    extend cancel lane with cancellation racing cache/fingerprint capture;
  - cache: fingerprint keyed on upstream content revision ⇒ upstream re-run invalidates
    downstream (content-hash reconciliation, not node-id); cancel/failed terminal does not seed
    the cache (`storePipelineStepOutputLocked` on real completion only); checkpoint-vs-journal
    count reconciliation on the same run.
- **WP-D (regression + ADR + edges)** — CMake registration; `ctest -R
  "workflow|durability|run_lock|lowering" -j1` twice; edge second pass (empty DAG, single-node
  DAG, cancel between creation and lock, missing/readonly lock dir, corrupt checkpoint ⇒
  structured error); ADR 0125 amendment recording the crash/lock/cache semantics clarified by
  this track (or an explicit "no clarification needed" decision in DECISIONS.md + ledger).

## TDD discipline per WP

One public seam → one failing (or, if already correct, hardening) test → minimal change →
atomic commit. Recovery assertions reconcile against ON-DISK artifacts (checkpoint JSON read
back independently of the resume code path), never against the recovery code's own view.

## Risk register

- Helper/spawn tests must be leak-proof: RAII kill + `waitForFinished`; ctest `-j1` anyway.
- `/tmp` output files from pipelines: use the QTemporaryDir checkpoint dir for outputs.
- The workflow lane has a long build; commits are batched per work package but each commit
  compiles standalone (verified by building the new targets before each `git commit`).
- QLockFile-only states (`Unknown`) are documented as non-Unix; tests assert the Unix-documented
  contract and skip nothing on Linux.
