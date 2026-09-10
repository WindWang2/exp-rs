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

## Round 1 — adversarial review (subagents), after local builds

(pending — to be filled after targeted tests pass)
