# DECISIONS — Track 10: Workflow Durability R4

D1 (2026-09-27, Phase 0) — **Charter drift corrections (measured)**:
`workflow_run_coordinator.cpp` is 68,241 B (charter: 69,745 B); the lock primitive on this
platform is flock(2) on an fd (QLockFile is the non-Unix fallback). The charter's "QLockFile at
line 29" is the forward declaration. `OwnerProbe::Unknown` is unreachable on Q_OS_UNIX by
construction (flock either conflicts ⇒ LiveOwner or not ⇒ NoHolder). Tests therefore assert the
Unix-documented two-state contract and treat `Unknown` as non-Unix-only.

D2 (2026-09-27, Phase 0) — **Avoid `tests/test_workflow_recovery.cpp` and
`tests/test_workflow_facts_11.cpp`**: occupied by open PR #1337. New lanes go to new files
(`test_workflow_crash_recovery_r4.cpp`, `test_workflow_run_lock_r4.cpp`) plus extensions of
cancel / cache_e2e / incremental_cache (unoccupied).

D3 (2026-09-27, Phase 0) — **Injector = composition of existing primitives, not a new
mechanism**: deterministic failure branches via `runtime/observability/fault_registry`
(`workflow_checkpoint.write` / `workflow_checkpoint.publish` sites already exist); real process
death via a `workflow_crash_helper` binary + QProcess + SIGKILL (the `helper_external_process`
tradition); staging via `geospatial/util/atomic_fs`; persistence via the production
`WorkflowCheckpointManager` / `WorkflowRunCoordinator` / `JobEngine` — the same stack
`test_workflow_recovery` already links.

D4 (2026-09-27, Phase 0) — **"Journal" terminology mapping**: the charter's journal is the
per-transition atomic checkpoint file (`checkpoint_<runId>.json`). Committed set = stepPlans
with `status=="Completed"` in the last intact checkpoint. Pre/post-commit windows are
distinguished by *which* fold managed to rename its persist before death, reconciled from the
file on disk.

D5 (2026-09-27, Phase 0) — **Cache lane scope**: ADR 0125's contract lives in
`ExecutionResultCache` + `makeExecutionFingerprintV2` + TaskCenter gating. WP-C tests the
public seams (fingerprint determinism/sensitivity, store-on-real-completion, terminal pruning)
from the test surface; no `src/processing` production edits planned. If testing exposes a real
defect there, the fix will be explicitly accounted here before it lands (charter whitelist
names `src/workflow` + `src/planner` for code; cache seams are exercised, not rewritten).

D6 (2026-09-27, Phase 0) — **src/planner**: no #1321-lowering recovery-semantics fix is
anticipated; the planner lowering (#1321) produces definitions for the same coordinator paths
already covered. Touches stay at zero unless a matrix case proves otherwise.

D7 (2026-09-27, Phase 0) — **Baseline red set policy**: existing red tests in the
`workflow` ctest lane (if any) are recorded in BASELINE.md §7 and excluded from
"this track's net change"; the gate is zero NEW failures + two consecutive green runs of the
union lane.

D8 (2026-09-27, Phase 0) — The `.goal-loop-ledger.md` is TRACKED: contrary to the charter's
"gitignored" assumption, the repo carries a per-track ledger at the worktree root that each
track replaces (history full of `docs(ledger):` commits). This track follows that convention;
the previous track's content is replaced, its history preserved in git.

D9 (2026-09-27, execution) — Master link-graph breakage + whitelist-internal enablement:
measured 301 test executables unbuildable at `15e5c66b5` (one unresolved symbol:
`sicnu_agent_loop::VerificationReport::aggregate` referenced by `libsicnu_agent.so`; PR #1335
owns the fix inside src/agent). Within this track's whitelist (tests/CMakeLists.txt only) the
fix is replicated for (a) this track's own targets, (b) the extended targets
(test_workflow_cancel, test_workflow_cache_e2e), (c) the 22 oracle-lane targets via one guarded
foreach — each marked REVERT-on-#1335. No src/agent edits. Everything outside the lane stays
broken exactly as master left it.

D10 (2026-09-27, execution) — Journal-lag honesty: measured that with a worker blocked in a
later step, a completed step's journal commit can lag indefinitely (best-effort persistence by
design — "the state on disk simply lags"). IP-2b therefore reads the committed set from disk at
kill time and derives its replay expectations from THAT: assertions reconcile against the
journal, never against a schedule assumption. Recorded in ADR 0177 section 1 and EVIDENCE
section 4.
