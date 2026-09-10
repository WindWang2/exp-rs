# TEST_MATRIX — Execution Plane 8.0

New suite: `tests/test_execution_plane_8.cpp` (RUN_SERIAL — singletons +
wall-clock scaling). Regression guard: existing suites (ep7, worker_host,
job_engine, task_center, workflow_run_coordinator, workflow_resume_provenance,
workflow_recovery, execution_fingerprint, execution_benchmarks).

| # | Requirement (track) | Test | Status |
|---|---|---|---|
| A1 | Priority order through ready heap (1 slot) | ep8 "Priority order holds through the ready heap" | new |
| A2 | Cancel of admission-held task, no stale launch, no strand | ep8 "Cancelling an admission-held task leaves no stranded work" | new |
| A3 | DAG chain drains in dependency order (parent promotion) | ep8 "DAG chains drain in dependency order" | new |
| A4 | Transient auto-retry re-enters admission via heap | ep8 "Transient auto-retry re-enters admission through the ready heap" + ep7 retry matrix | new+existing |
| A5 | Engine bucket queue: priority pick + exclusive drain order | ep8 "JobEngine bucketed queue" | new |
| A6 | No O(n²) cliff: 2k vs 10k drain ratio + absolute bound + zero stranded | ep8 "Short-job drain scales without the pre-8.0 admission cliff" | new |
| B1 | Dynamic availability: limit raise admits held work immediately | ep8 "Raising a resource limit admits held work" | new |
| B2 | Reservations/cancel-while-queued | covered by A2 (staging holds the reservation; cancel frees it) | new |
| C1 | Process-group containment (POSIX) escalates tree-wide | worker_process_guard.terminateTree unit behavior via ep7 worker cancel/timeout cases (real sicnu_worker); Windows branch compile-only on this host | new code, existing tests run it |
| C2 | Heartbeat op wire-compatible (old hosts ignore) | pool loop treats heartbeat as liveness (code path covered by ep7 e2e runs against real worker which emits heartbeats); explicit hang-window test requires env toggle — documented, not asserted in default runs | partial |
| D1 | Retry classification formalized + budget + evidence | ep8 retry test + TaskCenter log evidence (budget-exhausted line) + isTransientExecutionError public seam | new |
| E1 | Operator identity stamp round-trip (JSON additive) | ep8 "StepPlan operator identity stamp round-trips" | new |
| E2 | Resume re-executes when operator implementation changed; serves when equal | ep8 "Resume re-executes a served step only when the operator implementation changed" | new |
| E3 | Moved output re-hydrated from content-addressed pool (digest-proven) | covered in code + manual scenario; e2e assertion planned (pool env isolation) — see REVIEW_LOG follow-up | partial |
| E4 | Legacy checkpoints fail closed (no stamp + resolvable operator ⇒ re-execute) | implied by E2 scenario 2 mechanism; regression-guarded by existing coordinator resume tests (prefix-executor steps stay servable) | existing+new |
| F1 | remoteIdentity participates in fingerprint (stable/different) | ep8 "Remote identity token participates in the execution fingerprint" | new |
| F2 | Default resolver installed; local paths fail closed | ep8 "TaskCenter installs the remote identity resolver by default" | new |
| G | Committer convergence audit | CAPABILITY_MATRIX.md audit matrix (docs) | audit |
| H | Cancel×admission×shutdown race windows | existing ep7 queued-cancel/shutdown cases + A2 | existing |
| I1 | Trace events bounded + correlation | trace emit sites are Trace::enabled()-gated; sink contract covered by Verification 7.0 trace tests | wiring |

## Determinism / resource bounds

- All new tests use synthetic payloads (no GDAL rasters except resume
  identity's tiny text file); bounded wall-clocks (600s caps, generous
  machine-noise margins); no network (remote resolver default-install test
  only exercises the LOCAL fail-closed branch).
- 10k stress stays within the ep7 envelope (bounded drain deadline; no
  unbounded loops).
