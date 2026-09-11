# DOCS LEDGER — Execution Plane 8.0

Docs/help/diagnostic contracts affected by behavior changes, and their sync
status (done on this branch unless noted):

| Doc / contract | Change | Status |
|---|---|---|
| docs/adr/0063-task-center-rss-watermark-throttling.md | Admission pass re-architecture does NOT change the watermark semantics (global hold, timer re-arm preserved) | verified, no edit needed |
| CHANGELOG.md | Execution Plane 8.0 entry under [Unreleased] | planned (final integration commit) |
| Worker wire contract (worker_protocol.h header doc) | heartbeat op + capability documented in-source (wire stays v1) | done |
| Checkpoint format (StepPlan::toJson/fromJson) | additive optional `operatorImplStamp`; legacy readers unaffected; legacy checkpoints re-execute resolvable-operator steps (fail-closed) — documented in workflow_run.h field comment | done |
| Diagnostics / observability contract | new trace event kinds are plain strings in exp.trace.v1 records (schema-less); no catalog/help contract drift | verified |
| Help / catalog systems | no user-facing operator/catalog changes (no new algorithms; no capability vocabulary changes) | verified, no edit needed |
| env-var surface | SICNU_WORKER_HANG_TIMEOUT_MS (new, default off = prior behavior) | documented in local_worker_pool.cpp (single source) |

Untracked-doc rule: every env var / wire field / checkpoint field change in
this branch is documented in the header of the file that owns it.
