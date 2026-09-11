# MILESTONES — Execution Plane 8.0

## M1 — Admission scaling (WP-A) — code complete
- [x] TaskCenter incremental admission (counters, parent counts, children
  index, ready heap with epoch/serial invalidation, bounded scan,
  staging-time placeholders + fingerprint verification)
- [x] JobEngine priority-bucket queue + exclusive FIFO + O(log P) pick
- [x] ep8 tests: priority order, cancel-while-held, DAG order, retry
  re-promotion, engine buckets, 2k/10k scaling ratio
- [ ] Local run + timing evidence (blocked on machine contention from
  parallel 8.0-track builds; recorded in PERFORMANCE.md when available)

## M2 — Worker containment & liveness (WP-C) — code complete
- [x] worker_process_guard (POSIX setsid group + tree kill ladder; Windows
  Job Object kill-on-close, compiled-only evidence on this host)
- [x] Worker heartbeat frames (wire-compatible) + host hang window
  (SICNU_WORKER_HANG_TIMEOUT_MS, default off)
- [x] Escalation/teardown/shutdown tree-kill integration in pool + host

## M3 — Retry formalization (WP-D) — code complete
- [x] Public documented classifier seam
- [x] Budget-exhausted evidence in task log + trace retry events

## M4 — Resume 3.0 (WP-E) — code complete
- [x] StepPlan.operatorImplStamp (additive checkpoint field, fail-closed
  read)
- [x] Resume operator-identity gate
- [x] Moved-output re-hydration from the content-addressed pool
- [x] ep8 tests: stamp round-trip, operator-changed ⇒ re-execute / equal ⇒
  serve

## M5 — Remote identity (WP-F) — code complete
- [x] remote_identity_resolver (strong-ETag only, bounded session cache,
  Qt-free geospatial layer)
- [x] Default install via TaskCenter (host override preserved)
- [x] TaggedDerivationInput.remoteIdentity in the canonical fingerprint
- [x] ep8 tests: token stability/distinctness, local fail-closed install

## M6 — Committer convergence audit (WP-G) — complete (audit)
- [x] All entry paths traced; matrix in CAPABILITY_MATRIX.md; 7.0 fusion
  limitation verified already-resolved; GUI registration policy recorded as
  cross-track follow-up

## M7 — Dynamic availability (WP-B) + observability (WP-I) — code complete
- [x] Resource setters re-run admission
- [x] Trace emit sites (TaskCenter transitions, cache, resume)

## M8 — Verification & review
- [x] Full local test sweep: ep8 105/13, task_center 342/31, job_engine
  446/34, ep7 48/9, worker_host 56/12, coordinator 171/10,
  resume_provenance 67/2, fingerprint 60/16, cache_e2e 189/12,
  scheduler3 11/4 — all green
- [x] Adversarial review (2 read-only reviewers): 2×P0, 6×P1, 6×P2, 8×P3 —
  all fixed; post-review local verification caught one more P0 regression
  (isolatedRoute accounting order) — fixed and re-verified
- [ ] Docs ledger sync, CHANGELOG, final diff inspection, push, PR
