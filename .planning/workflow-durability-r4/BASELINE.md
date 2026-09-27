# BASELINE — Track 10: Workflow Durability R4 (crash recovery / locks / cache coherence)

Date: 2026-09-27. All numbers measured on the isolated worktree
`/home/kevin/project/exp-rs-workflow-durability-r4` (branch `hardening/r4-workflow-durability`),
created from `origin/master`.

## 1. Upstream anchor

| Item | Prompt claimed | Measured now | Verdict |
|---|---|---|---|
| origin/master SHA | `15e5c66b5` | `15e5c66b543ef3874cb929f17529ef456bd6c059` | match |
| ahead/behind | work at 0/0 | branch created AT origin/master (0/0) | match |

## 2. Counting anchors (§3.1 of the charter)

| Anchor | Claimed | Measured | Command |
|---|---|---|---|
| `src/workflow` file count | 48 | **48** | `find src/workflow -type f \| wc -l` |
| `workflow_run_coordinator.cpp` size | 69,745 B | **68,241 B** (drift −1,504 B; #1301-#1333 churn) | `stat -c%s` |
| `tests/test_workflow*.cpp` | 27 | **27** | `ls tests/test_workflow*.cpp \| wc -l` |
| `test_workflow` refs in tests/CMakeLists.txt | 102 | **102** | `grep -c test_workflow tests/CMakeLists.txt` |
| Lock primitive | QLockFile (h:29) | **flock(2) fd on Q_OS_UNIX**; QLockFile only on non-Unix (forward decl h:29, member h:77). "kernel releases it on ANY process death" comment at h:10-13 | read `workflow_run_lock.h` |
| `OwnerProbe` 3 states | 3 (h:61) | **3**: `NoHolder / LiveOwner / Unknown` at h:63. NOTE: on Q_OS_UNIX `probeOwner` returns only NoHolder/LiveOwner; `Unknown` is reachable only on the QLockFile (non-Unix) path | read `workflow_run_lock.cpp:170-213` |
| ADR 0125 | exists | exists (`docs/adr/0125-temporal-workspace.md`, 207 lines) | ls |
| Recovery/lock/cache-related of the 27 | ≥7 | cancel, checkpoint_cache, recovery, resume_provenance, cache_e2e, incremental_cache, durability_13, run_coordinator — **≥8** | ls |

## 3. Open PRs (in-flight) and file-overlap map

Measured `gh pr list --state open` (7 open; the charter listed 3):

| PR | Files | Workflow/test/planner overlap | Decision |
|---|---|---|---|
| #1334 (security) | 30 | none | ignore |
| #1335 (build/CI restore) | 19 | none | ignore |
| #1336 (i18n/help) | 13 | none | ignore |
| #1337 (workflow+agent semantics) | 25 | **`tests/test_workflow_recovery.cpp`, `tests/test_workflow_facts_11.cpp`** | **Avoid both files for edits.** Rationale: #1337 already rewrites the recovery lane's prose/assertions; any parallel edit there is a guaranteed rebase conflict with zero added coverage value. Our lock/cancel/cache/crash-matrix work lives in NEW files + cancel/cache_e2e/incremental_cache/checkpoint_cache, which #1337 does not touch. |
| #1338 (io/processing atomic publish) | 29 | none | ignore (adjacent but disjoint paths) |
| #1339 (i18n tr() coverage) | 38 | none | ignore |
| #1340 (operator NoData semantics) | 24 | none | ignore |

No open PR occupies `src/workflow/**` or `src/planner/**`. Rebase risk: LOW (avoid the two
#1337 test files).

## 4. Review materials read (Phase 0)

- ADR 0125 (`docs/adr/0125-temporal-workspace.md`) — execution-cache contract: fingerprint
  `makeExecutionFingerprintV2(alg, ver, RFC-8785 params, TaggedDerivationInput{assetId@revision})`
  keyed on identity+revision **never path**; `storePipelineStepOutputLocked` on real completion
  only; terminal Failed/Canceled pruning; #720 `clear()` regressions fixed.
- #727 ownership contract (`workflow_run_lock.h` header + `workflow_checkpoint.h`
  `recoverInterruptedRuns` doc): flock per run next to checkpoint; acquire BEFORE touching the
  checkpoint proves the previous owner is gone; lock files are never unlinked.
- #1323 atomic journal publish = `WorkflowCheckpointManager::saveCheckpoint` tmp→fsync→rename
  contract + fault points `workflow_checkpoint.write` / `workflow_checkpoint.publish`
  (`src/workflow/workflow_checkpoint.cpp:106,126`), plus the D17 lanes
  `d17_checkpoint.publish` / `d17_provenance.publish` in `pipeline_run_coordinator`.
- #1314 crash-orphan adoption lineage = the lock-aware `recoverInterruptedRuns` +
  `reconcileToInterrupted` + ghost quarantine (`electCheckpoints`, Track 13).
- Deterministic fault-injection primitive to REUSE (no second one):
  `src/runtime/observability/fault_registry.h` (`armFault/shouldFail`, Modes NextN/Always/EveryNth,
  RAII `ArmedFault`); real-process tradition: `tests/helper_external_process.cpp` +
  QProcess-spawning test lanes (`exprs_ep_helper` pattern).

## 5. Open issues

`gh issue list --state open` measured: 0 open issues (matches charter).

## 6. Scope boundary declaration

- Whitelist: `src/workflow/**`, `src/planner/**` (only if #1321-lowering recovery semantics force
  a minimal fix, accounted per-hunk), `tests/**` (incl. tests/CMakeLists.txt), `docs/adr/**`,
  `.planning/workflow-durability-r4/**`.
- Out of scope (untouched): `src/agent/**` (Tracks 8/9), `src/experiment/**` (Track 11),
  `src/processing/**` production code (the cache seams are exercised through their public test
  surface; if a one-line fix inside `src/processing` becomes unavoidable it will be accounted
  here first), new workflow features/node types/execution planes (zero new directions).

## 7. Baseline ctest red/green (workflow lane) — MEASURED

Fresh build dir `build-wf4` (Debug, Ninja, ENABLE_TESTS=ON, `-j2`).

**Master link-graph breakage (the dominant baseline fact).** At
`15e5c66b5` `libsicnu_agent.so` references
`sicnu::agent_loop::VerificationReport::aggregate` without linking
`sicnu_agent_loop`, so every executable that pulls the agent `.so` fails to
link: **301 targets repo-wide** (`build-k0.log`, ninja `-k 0`), including 22
of the durability oracle lane's targets (test_workflow_run_coordinator,
test_workflow_recovery, test_workflow_runtime, ... — full list in the
tests/CMakeLists.txt band-aid block). The open PR **#1335** ("restore master
build/CI — link graph, ctest discovery") owns the proper fix; the breakage
is pre-existing master state, NOT introduced by this track.

**Track enablement (whitelist-internal).** This branch band-aids exactly the
22 lane targets (and this track's own new/extended targets) with
`$<LINK_LIBRARY:WHOLE_ARCHIVE,sicnu_agent_loop>` inside tests/CMakeLists.txt
(the pattern `test_agent_loop_journal_durability` already used), marked
REVERT-on-#1335. After the band-aid, all 22 lane targets link.

Baseline `ctest -R "workflow|durability|run_lock|lowering" -j1` recorded in
`baseline-ctest.log` next to this file; the red/green summary and the
already-red set are appended below once the run completes.

## 8. Refined crash-recovery invariants (charter §Timing-invariants, revised in Phase 0)

1. **Commit boundary is the checkpoint file**: "journal" = `checkpoint_<runId>.json` written by
   `saveCheckpoint` (tmp+fsync+rename, #1323). Pre-commit death ⇒ the run's committed set is
   whatever the LAST intact checkpoint says (older or none); post-commit death ⇒ the committed
   set in the file is served and never re-executed (subject to the #750 identity gate and the
   8.0-WP-E operator-identity gate, both fail-closed by design).
2. **No replay of committed steps**: resume serves a Completed step only when its output file
   still passes stat+digest identity (#750) and the operator implementation stamp matches
   (8.0 WP-E). Any mismatch re-executes — that is fail-closed re-execution, not replay.
3. **Lock before run-state**: flock acquired before dispatch / before checkpoint inspection
   (both `startTrackedPipeline` and `resumeRunImpl`); released only at finalize/resume-swap/
   submission-failure. A run observed in an active state WITHOUT a live owner is stale by
   construction and reconciled inline (`resumeRunImpl`) or by the recovery pass
   (`recoverInterruptedRuns`), which skips runs whose lock is held by a live process.
4. **Cancel is sticky through the state machine**: `Cancelling` is a persisted state; crash
   during cancel reconciles to `Interrupted` (resumable), completed steps stay completed, and a
   terminal run refuses `transitionTo(Cancelling)` (no post-terminal resurrection via the
   guarded transition table — `workflow_run.cpp:117`).
