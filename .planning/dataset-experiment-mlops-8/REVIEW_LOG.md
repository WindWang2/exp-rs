# REVIEW_LOG — dataset-experiment-mlops-8

## Round 0 — self-review (primary agent), pre-build

Scope: full diff `origin/master...HEAD` at `6b224986fd`. Findings fixed
immediately (commits `78c78195b2`, `6b224986fd`):

- **[F1, P1] Resume records could never complete.** After a coordinator
  resume swap, no `Running` is re-emitted for the ORIGINAL run id, so an
  auto-recorded run sat in `Interrupted` and the store legally refused
  `Interrupted → Completed` (bad_transition) — a resumed run could never
  surface as a successful experiment. Fix: the bridge advances
  Interrupted→Running explicitly when completion arrives for an interrupted
  record (the only truthful path), covered by a dedicated unit case.
- **[F2, P2] `ensureExperiment` wrote `m_experimentId` without the bridge
  mutex** while the event path reads it under lock. Fixed (consistent
  bridge-mutex discipline; lock order bridge→store everywhere, no inverse
  edges exist).
- **[F3, P2] E2E cancel assertion over-specified the engine.** A racing
  cancel can truthfully land Failed (executor error first) or Cancelled
  (cascade wins). The test now asserts the actual contract — experiment
  state equals execution truth — instead of a specific engine outcome.
- **[F4, P3] Dead helper `statusToString`** removed; explicit
  `<QEvent>`/`<QMap>` includes added.
- **[F5, P2] E2E success artifact count was wrong** (2 steps → 2 artifacts,
  not 1); assertion now checks both artifacts and the producing-step role.

Deliberate design points a reviewer should probe (with rationale):

1. `runIdsByExecutionRef` is a paged JSON scan, not an index — cold-path
   only; the live bridge never calls it per event (in-process map). Scale
   probe covers the scan cost.
2. Stale reconciliation acquires a run's flock to prove no live owner, then
   releases immediately — a concurrent resume from another process in that
   window would see Interrupted (truthful at that instant) and continue the
   same record afterwards.
3. Completed-checkpoint stale runs are REPORTED, not closed: closing as
   success without artifact evidence would fabricate output truth.
4. MCP pins attach pre-submission keyed by definition id; concurrent runs
   of the SAME definition share pin defaults (documented); identity is
   store-enforced immutable after start.
5. Transitional workflow states (Created/Planning/Ready/WaitingResource/
   Cancelling) and post-resume ghost Running events are IGNORED — ignoring
   records nothing; the ghost's registration lifetime guarantees the
   filter is exact (tracked at emit, untracked at delivery).

- **[F6, P1] Resume-ghost Running events could dangle forever.** A resume
  swap emits Running for the fresh submission and then unregisters it; the
  queued delivery would record a ghost execution that can never terminate.
  The monitor now skips untracked Running events whose checkpoint file is
  absent (ghosts' checkpoints are deleted at the swap; real runs persist
  theirs before dispatch). Terminal/Interrupted events always record —
  startup-recovery Interrupted reaches the bridge untracked by design.
  The resume E2E asserts exactly one record exists (no ghost).
- **[F7, P3]** `experiment:compare` now surfaces experiment_context
  (baseline/treatment tags) — closes the WP-F "baseline tags" gap additively.

## Round 1 — adversarial review (subagents), after local builds

Two read-only subagents ran the full diff (`ab57d99da9`): A = architecture/
correctness, B = tests/performance/portability. Combined verdict:
**P0: 0, P1: 4, P2: 7, P3: 19**. Remediation (all P1/P2 fixed; P3 fixed or
justified below):

- **[A-P1-1] Recording was process-wide once enabled.** The monitor now
  records ONLY submissions that call `recordSubmission()` (per-submission
  opt-in); the MCP handler binds each accepted submission. `optInResume()`
  covers continuation of an already-recorded story (resume surfaces).
- **[A-P1-2] Pin registry was last-writer-wins across submissions** — a
  pipelined second submission of the same workflow could retroactively
  re-pin the first run's record. Recording is now SYNCHRONOUS per
  submission (recordSubmission right after the tracked submit; pins bound
  to that run's start transition); the queued signal path is an idempotent
  continuation.
- **[A-P1-3] Docs claimed stale reconciliation closes Completed records.**
  Corrected in ADR 0143 + auto-recording.md (completed checkpoints are
  reported, never closed) and `flush()` now runs in McpServer teardown.
- **[B-P1-1] Whole-binary E2E hang (structural).** Root causes fixed: the
  cancelled-case executor spun on a deadline-less loop over by-reference
  captures (any lost race = hung worker + shutdownForTests join forever).
  The executor loop is now hard-bounded and captures by value; CTest
  TIMEOUT 600 added; PERFORMANCE.md wording corrected.
- **[A-P2-1] Silent pin drops (negative seed; id-less pipeline).** Both are
  typed `INVALID_ARGUMENTS` refusals now.
- **[A-P2-2 / fix] `markSucceeded` replaced the metrics document wholesale**,
  erasing `interrupt_note` on resumed runs — it now MERGES like the other
  markers.
- **[B-P2-2] `enable()` with a different experiment db silently kept the
  first store.** The monitor is now bound to one db path (typed refusal on
  mismatch); an absent dataset db resets verification state.
- **[B-P2-1] MCP surface untested.** Two new test_mcp_server cases
  (recording produces a Completed run + experiment_run_id in the response;
  missing experiment_id / negative seed refused) plus an
  `experiment_context` assertion in the data-platform surface test.
- **[A-P2-3] Thread affinity documented + asserted** (`Q_ASSERT` in
  enable/recordSubmission; header contract).
- **[B-P2-4] CTest TIMEOUT 600** for test_mlops8_e2e.
- P3 fixed: dead `statusToString` removed; live ref→runId map bounded
  (10k, falls back to the cold-path scan); jsonCpp conversion truncation
  now marked with "…truncated"; checkpoint filename consolidated in the
  adapter; debug prints removed; scale test asserts ensureExperiment;
  misleading test name + stale comment corrected; compare-loop double-copy
  replaced with references; "Succeeded" → store vocabulary ("completed")
  in tool text and changelog; split-pin doc wording matches code.
- P3 justified/documented: RUN_SERIAL on discovered tests is ineffective
  (pre-existing house pattern; cases are self-contained); the flock probe
  treats probe failure as "cannot answer" (report, never close — fail
  closed); `runIdsByExecutionRef` needle-matching retained for the
  documented cold path (verified: compact serializer emits no spaces, the
  needle includes the closing quote); forward-decl placement in
  mcp_server.h; sicnu_add_test's heavy default link set (house pattern).

### Round-1 fix that round-1 reviews caught only in passing

- **[critical, found during remediation] Data race on the workflow
  definition**: `workflowRunToExecutionEvent` read `definition()` (the
  run's own mutex is deliberately escaped there) while the coordinator's
  fold thread serialized the same aggregate — heap corruption, SIGSEGV
  (reproducible; coredump-backed). The conversion now takes ONE locked
  `run.toJson()` snapshot and reads everything from it.
- **[critical, found during remediation] First submission lost its
  terminal event**: `enable()` (which owns the signal connection) ran
  after the tracked submit, so both transitions were emitted before the
  connection existed. The MCP handler now enables BEFORE submission and
  only binds the run after. The recording test binary also lacked a
  QCoreApplication — added (queued delivery requires an event queue).

Re-run after remediation: mlops8 bridge 150 ✓, scale 19907 ✓, e2e 6/6 ✓,
mcp_server 3934 ✓ (incl. new surface cases), platform7 228 ✓,
experiment_evaluation 162 ✓, data_platform_surface 101 ✓.


(pending — to be filled after targeted tests pass)
