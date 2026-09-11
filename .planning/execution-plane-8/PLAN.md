# PLAN — Execution Plane 8.0

Order chosen to de-risk: performance core first (largest, best-tested),
then worker containment, then identity/resume, then audit-only work.

## Milestone 1 — Admission scaling (WP-A) + evidence
1. Capture baseline: build + run test_execution_plane_7 10k stress on this
   machine (Release), record drain time in PERFORMANCE.md.
2. TaskCenter: incremental active counters via `setTaskStatusLocked`;
   incomplete-parent counts; ready heap (epoch/priority/serial); bounded
   scan; staging-time placeholders + fingerprint verification.
3. JobEngine: priority-bucket queue + exclusive counter + iterator-hint
   cancel.
4. New tests (test_execution_plane_8): priority order, cancel-while-queued
   under admission, 1k/10k drain with bounded runtime wall-clock assertion,
   counter-parity invariant, exclusive ordering, retry re-promotion.
5. Re-run 10k stress: expect ≥ 5× improvement (Release), no stranded tasks.

## Milestone 2 — Worker containment & liveness (WP-C)
1. `worker_process_guard` + pool/host integration (spawn, escalation,
   teardown, shutdown).
2. Worker heartbeat op + host hang detection (default off; test-enforced).
3. Tests: orphan-containment (spawn worker that forks a child, kill host,
   assert tree death — POSIX; Windows logic compiled but only
   unit-testable parts executed), hang→cancel ladder, teardown boundedness.

## Milestone 3 — Retry formalization (WP-D)
1. Extract retry classification into a named, documented decision function
   with a machine-readable reason string; stamp attempt/cause into the task
   log + trace + (new) result payload evidence on final failure.
2. Tests: budget exactness, no-retry-after-cancel, no duplicate terminal
   notifications, DAG identity, evidence fields.

## Milestone 4 — Resume 3.0 (WP-E)
1. StepPlan operatorImplStamp (write at completion; additive JSON).
2. resumeRun gate: stamp compare + pool-based moved-output re-hydration.
3. Tests: operator change ⇒ re-execute; same stamp ⇒ serve; moved output +
   pool object ⇒ re-hydrate and serve; no pool object ⇒ re-execute; legacy
   checkpoint behavior documented + tested.

## Milestone 5 — Remote identity (WP-F)
1. Remote-source identity adapter (strong-ETag only) behind
   `execution_identity_resolver`; consult from fingerprint collector.
2. Tests: cacheable when strong ETag stable; uncacheable when weak/none/
   offline; identity changes when ETag changes (simulated validator).

## Milestone 6 — Committer convergence audit (WP-G)
1. Trace every entry path (GUI/CLI/MCP/harness/workflow/adapters/model/
   fusion/worker) to its commit behavior; write the matrix into
   CAPABILITY_MATRIX.md.
2. Close provable production bypasses at the narrowest seam; otherwise
   document the guard.

## Milestone 7 — Concurrency audit + observability (WP-H/I)
1. Deterministic fault/stress tests for cancel×admission×shutdown windows;
   review mutex scopes flagged in BASELINE §5.
2. Trace emit sites + a bounded-events test.

## Milestone 8 — Review, docs, integration
1. Freeze features; adversarial review with the 2-subagent budget.
2. Remediate P0/P1 (all), P2 (all reasonable), P3 (fix or justify).
3. Docs ledger: help/catalog/diagnostic contracts touched by behavior
   changes; FINAL_REPORT.md; PR.
