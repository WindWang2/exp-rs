# PLAN — milestones, each ending in a coherent commit

Order chosen by correctness priority (data-loss first), dependency-aware.

- **M0 Startup** (done): dossier, worktree `zcode/data-runtime-governance-4`.
- **M0.1 Build bring-up**: configure worktree build (Release/Ninja/tests ON), build
  owned test targets at baseline to prove the harness before changes.
- **MA Milestone A** — project/governance data-loss invariants (#746 #751 #752):
  v3-seen downgrade guard, cache invalidation on project transition, aggregated
  restore diagnostics, busy_timeout on all connections, real WAL checkpoint via
  `sqlite3_wal_checkpoint_v2(TRUNCATE)` + sidecar-consistent snapshot policy,
  read-only surfacing. Destructive fault-injection tests.
- **MB Milestone B** — store hardening/scale (#758 1–4): Result-returning writers
  with checked step/COMMIT + rollback; alias owner collision symmetry; true COUNT(*)
  for summary; statement reuse / batch transactions in bulk paths; stable pagination.
  100k benchmark rerun.
- **MC Milestone C** — artifact/content identity (#758 5–7 + identity model doc):
  CAS eviction must check all live digest users (reference-safe), incremental
  storage accounting, lineage deletion direction (outgoing only), ReproBundleExporter
  const_cast fix. Written identity model: logical asset → revision → artifact →
  digest → execution fingerprint → lineage.
- **MD Milestone D** — cache correctness for external mutation (#749): stat
  registered non-chained inputs into input identity at store time (fail-closed),
  verify-before-hit, wire `DataManager::notifyExternalContentChange` to revision
  bump + cache invalidation seam; mtime-granularity mitigation via size+digest
  verification policy. Tests: same-size replacement, coarse mtime, external rewrite.
- **ME Milestone E** — crash-resume identity (#750): completion identity stamp on
  StepPlan checkpoint (size+mtime minimum, digest where affordable); resume gate
  re-verifies; mismatch → re-execute; never feed foreign bytes downstream.
- **MF Milestone F** — truthful run state (#754): wire real terminal states from
  WorkflowRunCoordinator/TaskCenter into governance runs (recordRun seam); stop
  fabricating Completed; orphan logic reflects failed runs.
- **MG Milestone G** — ImportCenter correctness (#753): cancel breaks at batch
  boundary; partial tally truthfully reported.
- **MH Milestone H** — worker/runtime maturation: bounded warm pool, health/lifetime,
  crash detect/restart, per-job timeout/cancel escalation, telemetry, safe shutdown —
  on the existing runtime/worker architecture, no second scheduler.
- **MI Milestone I** — remote data plane: validator/ETag identity, offline behavior,
  retry bounds, quota; no UI-thread network I/O (build on test_remote_source_cache).
- **MJ Milestone J** — fault-injection matrix + scale certification (normal/100k/
  opt-in 1M), FAULT_MATRIX doc + tests.
- **M-DOC** — PROJECT.md refresh (living doc), CONTEXT.md, CHANGELOG, ADRs (0130+),
  module READMEs, DOCS_LEDGER audit.
- **M-REV** — 6-lens adversarial review, fix P0/P1, document accepted debt.
- **M-FIN** — full targeted verification, clean tree, coherent commits, PR.

## Commit plan

1. `chore(planning): Reliability 4.0 dossier` (this dossier)
2. `fix(governance): project save/restore data-loss invariants (#746 #751 #752)`
3. `fix(governance): SQLite store write/commit hardening + scale (#758)`
4. `fix(data): reference-safe artifact identity, lineage direction, bundle const-cast (#758)`
5. `fix(cache): external-mutation invalidation for registered inputs (#749)`
6. `fix(workflow): crash-resume completion identity (#750)`
7. `fix(governance): truthful workflow run states (#754)`
8. `fix(governance): ImportCenter batch-boundary cancellation (#753)`
9. worker/runtime + remote-plane hardening commits
10. `test(governance): fault-injection matrix + scale certification`
11. `docs: Reliability 4.0 documentation pass`
