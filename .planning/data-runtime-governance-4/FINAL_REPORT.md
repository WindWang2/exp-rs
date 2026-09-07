# FINAL REPORT — Data Plane, Runtime, Governance & Reproducibility Reliability 4.0

Branch `zcode/data-runtime-governance-4` → `master`. Base `58eb196baa`.
Commits (oldest→newest): `445b0646ba` (Milestones A–G), `167756f71d` (H–J),
`8a62e82f09` (docs), `cda06126b1` (adversarial-review remediation).

## Scope & issue closure

| Issue | Severity | Resolution (summary) | Evidence |
|---|---|---|---|
| #746 corrupt DB → v3→v1 downgrade + cross-project bleed | P1 | cache-on-read + downgrade guard + integrity-probed save fallback; cache/v3Seen cleared on every store-path change and project transition | test_workspace_project_v3 (fault trio + stale-warning suite) |
| #749 registered-input external mutation never invalidates cache | P2 | store-time input stat binding + lookup validation (both tiers incl. persistent pool); fingerprint content digests (bounded); bounded DataManager watcher → revision bump | test_workflow_cache_e2e (#749), test_workspace_services (watcher) |
| #750 resume trusts any non-empty file | P2 | StepPlan completion identity (stat + digest ≤ 256 MB); resume gate re-verifies, fail-closed; legacy checkpoints re-execute | test_workflow_run_coordinator (#750) |
| #751 snapshot WAL checkpoint no-op | P2 | checkpointForBackup steps wal_checkpoint(TRUNCATE), requires busy==0; refusal on failure; sidecar policy corrected | test_workspace_services WAL snapshot suites |
| #752 restore swallows store failures | P2 | aggregated workspace.restore_failed; store_read_only/store_unavailable surfaced (GUI status bar + qWarning, CLI stderr, tests) | test_workspace_project_v3 (newer-schema) |
| #753 ImportCenter cancel ignored in registration | P2 | batch-boundary break + truthful partial tally | test_workspace_services cancel suite |
| #754 fabricated run states | P2 | coordinator runStateChanged signal → recordRun merge; mirror never fabricates; orphan logic covers Failed/Canceled/Interrupted | test_workspace_services run-mirror suite |
| #758 store hardening bundle | P3 | checked BEGIN/step/COMMIT everywhere (rollback on failure), linkRunOutput→Result, alias-collision symmetry, COUNT(*) entityCounts, prepare-once statement cache, reference-safe CAS eviction, incremental storage accounting, direction-aware lineage deletion, bundle const_cast removed | test_governance_store (lineage/alias/counts), test_fault_injection (CAS eviction) |

Also delivered: bounded warm worker pool (`LocalWorkerPool`, tested seam, opt-in
wiring tracked), remote HTTP config single-source-of-truth, 24-row fault matrix
(`docs/architecture/FAULT_MATRIX_4.md`), 100k scale re-certification
(`.planning/data-runtime-governance-4/SCALE_BASELINE.md`), ADR 0130, PROJECT.md
refreshed as a living document (resolves #760's substance), CHANGELOG +
CONTEXT.md + DOCS_LEDGER updated.

## Verification (all local, sequential, `QT_QPA_PLATFORM=offscreen`)

22 targeted suites green — 2427 assertions total, including 6 fault-injection
suites and 100k stress. Per-suite counts in REVIEW_LOG Round 1. No claim above
depends on online CI.

## Review

4 independent adversarial reviews (6 lenses) over the whole diff; 1 P0 + 7 P1s
found and fixed (incl. a P0 the reviews caught: untracked new files), plus
actionable P2s (leaks, unchecked BEGINs, probe throttling, lock-scope hashing,
queued mirror, watcher re-arm). Accepted debt is enumerated with rationale in
`.planning/data-runtime-governance-4/REVIEW_LOG.md` (corrupt-while-open
detection best-effort; second-process snapshot window; unwired pool seam;
resume input re-validation scope; digest cost windows).

## Known limitations / follow-ups

- Concrete HTTP validator for remote sources (ETag/Last-Modified) needs an
  app-layer implementation — `src/data` is network-free by design.
- Worker pool production wiring (opt-in executor) is a tracked follow-up.
- Full-snapshot consistency under a second-process writer wants the SQLite
  backup API.
- Legacy (pre-identity) checkpoints re-execute on resume — intentional,
  documented fail-conservative migration.
