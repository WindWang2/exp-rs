# ORACLES — ds41-workflow-durability-13

Every gate must pass TWICE consecutively (final round) before declaring done. All runs:
`QT_QPA_PLATFORM=offscreen`, `-j1`/`-j2` max, targeted ctest, `--output-on-failure`.

## O1 — Strong artifact identity (WP1)
- A >2 MiB artifact whose MIDDLE bytes are rewritten (size + mtime preserved via
  `restoreMtime`) is NOT served as a CacheHit under `ArtifactIdentityMode::Auto`/`Full`
  (which record `sha256full:`); it recomputes. Red-first: the same fixture under `Fast`
  (`sha256fl:`) IS served today — proving the gate has discrimination, not vacuous passing.
- `Fast` mode stays byte-compatible with the legacy scheme (`sha256fl:` tag, head/tail window).
- Resume verification is tag-driven: a recorded `sha256fl:` verifies with the fast scheme and a
  recorded `sha256full:` with the full scheme; an unknown tag fails closed (recompute).
- The full hash streams in bounded chunks with a cancel poll; a cancel during the hash aborts it
  within one chunk and marks the node Cancelled (never Failed, never a false success).
- Evidence: adversarial fixtures in `tests/test_workflow_checkpoint_cache.cpp` x2.

## O2 — Run-id grammar / lineage envelope (WP2)
- Reproduction (red-first): two legitimate user runs `foo` and `foo_resume` saved as checkpoints
  are BOTH quarantined-down to one by `electCheckpoints` on master; after the fix both survive
  and both are recoverable.
- New-format (version 2) checkpoints carry an explicit `attempt` (and ghost `resumeOf`) envelope;
  election groups ghosts by the DECLARED lineage, never by filename suffix stripping.
- Legacy version-1 checkpoints still load with clear semantics (attempt=1) and still elect by the
  legacy suffix rule (migration recognizes legacy ghosts) — the existing fault-injection ghost
  election test passes unchanged.
- A version-3 (future) checkpoint is refused with a named error (fail closed).
- Fuzz: weird runIds — `_resume`/`resume_`/`__resume`/`_resume_resume` suffixes, interior dots,
  Unicode, path traversal (`..`, `.hidden`, `a/b`), empty, 129-char, duplicates — all either
  safely refused or never mis-grouped; parser caps (16 MiB) still enforced; no crash, no
  fabricated success.
- Evidence: `tests/test_workflow_durability_13.cpp` + `tests/test_fault_injection.cpp` x2.

## O3 — Coordinator mutator affinity (WP3)
- Every public mutator (`startRun`, `resumeFromCheckpoint`, `setExecutor`, `setMaxParallelism`,
  `requestCancel`) called from a foreign thread while a run is active produces NO race: statuses
  stay internally consistent, the run completes correctly, and an already-active coordinator
  still refuses a second start (no UB).
- `requestCancel` from a foreign thread returns promptly (flag-first) even while the affinity
  thread is inside a multi-second artifact hash; the hash aborts within one chunk.
- Destroying the coordinator from a foreign thread mid-run does not crash, deadlock, or leave a
  stale callback (run under the sanitizer lane where feasible; stress loop otherwise).
- Evidence: new cross-thread cases in `tests/test_workflow_checkpoint_cache.cpp` x2.

## O4 — Recovery stress (WP4)
- Crash injected at each D17 atomic-publish boundary (checkpoint tmp-write, pre-rename,
  provenance publish): the previous checkpoint is never torn, the failure path leaves no
  residue, and resume still works from the surviving file.
- Cancel during full-hash (O3) plus restart-style recovery: a fresh manager recovers an
  Interrupted run from disk; a corrupt checkpoint is skipped without blocking recovery; a
  corrupt provenance file never affects resume.
- Concurrent foreign-thread readers (`getAllStatuses`/`isRunning`/`checkpointPath`) hammering the
  coordinator while nodes execute: no torn reads, no crash, completion correct.
- Evidence: new cases in the D17 + durability lanes x2.

## O5 — Regression + final gate
- Full targeted families green TWICE consecutively:
  `ctest -R "test_workflow_checkpoint_cache|test_workflow_durability_13|test_workflow_recovery|test_fault_injection|test_workflow_ir_v2|test_workflow_composition|test_d17_workflow_pipeline_e2e"`.
- `git diff --check` clean; worktree clean; every intentional change committed.

## Supporting
- Independent review (separate agent, reads `origin/master...HEAD` itself): P0=0, P1=0 before PR;
  second pass after fixes.
- Gate potency: at least one new test proven to catch an injected/back-out defect (mutation).
