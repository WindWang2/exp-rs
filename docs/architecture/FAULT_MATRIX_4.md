# FAULT MATRIX — Data Plane, Runtime, Governance & Reproducibility 4.0

Extends `.planning/execution-data-plane-3/FAILURE_MATRIX.md` to the governance /
snapshot / cache / resume / import / worker surfaces (goal series 4.0, milestone J).

Contract for every row: the system ends in a **predictable, recoverable state** —
typed diagnostics, cache miss, re-execution or refusal — never silent wrong data.

| # | Fault | Expected safe behavior | Coverage (test / contract) |
|---|-------|------------------------|----------------------------|
| 1 | Corrupt governance SQLite (store closed at open) | open fails loudly; project read caches the v3 document; save re-persists it (never v1) | `test_workspace_project_v3` "Corrupt governance DB never silently downgrades"; `WorkspaceService::storeIntegrityOk` |
| 2 | Corrupt governance SQLite (corrupt while store open) | save probes integrity, falls back to the cached document; store writes fail with typed `store.*` diagnostics | `storeIntegrityOk` contract + row-1 suite; probe = `PRAGMA quick_check` |
| 3 | Newer-schema (read-only) store | `workspace.store_read_only` + `workspace.restore_failed` warnings; document wins on next save | `test_workspace_project_v3` "Newer-schema store surfaces restore failure" |
| 4 | Hot WAL at snapshot time | `wal_checkpoint(TRUNCATE)` folds the log; snapshot refuses (typed error naming the checkpoint) when checkpointing fails | `test_workspace_services` "Snapshot preserves governed writes that live only in the WAL"; `GovernanceStore::checkpointForBackup` |
| 5 | Concurrent writer during snapshot | snapshot either succeeds with an integrity-checked DB or refuses naming the checkpoint; never a torn copy | `test_workspace_services` "Snapshot under a concurrent writer stays openable" |
| 6 | Failed COMMIT / interrupted transaction | checked `Impl::commit()` rolls back and returns a typed `store.commit` failure; no dangling transaction cascade | `governance_store.cpp` commit() contract; writers return `Result` (issue #758-1) |
| 7 | Read-only project directory / permission failure | store opens read-only → row 3 behavior; meta writes are advisory and roll back cleanly | row-3 suite; `setMeta` rollback contract |
| 8 | Concurrent governance writers (multi-thread) | single connection guarded by a mutex + `busy_timeout=5000` on every connection | store thread contract; row-5 test drives a writer thread |
| 9 | Corrupt persistent cache object | digest re-verification at lookup → miss + entry drop, never a wrong serve | `test_fault_injection` "corrupted pool object self-heals" |
| 10 | Missing cached artifact | stat validation erases the entry → miss → re-produce | `test_workflow_cache_e2e` (existing self-heal coverage, unchanged) |
| 11 | Externally replaced input (same size, same mtime) | content digest in the fingerprint → different identity → miss | `test_workflow_cache_e2e` "Out-of-band same-size rewrite … (#749)" |
| 12 | External content change on a registered asset | bounded watcher advances the revision; revision-keyed identity invalidates | `test_workspace_services` "DataManager advances the asset revision …" |
| 13 | Killed worker (process isolation) | typed `worker crashed:` host error; pool retires the worker and spawns a replacement for future jobs | `test_worker_host` crash isolation; `test_worker_host` "worker pool reports a typed failure for a broken worker program" |
| 14 | Worker job timeout | typed `worker timeout:`; worker retired; bounded fail-fast on unspawnable programs | pool contract (`jobTimeout`, `spawnAttempts < 3`); `__hang__` cancel/timeout suites |
| 15 | Killed workflow (process crash mid-run) | startup recovery reconciles to Interrupted; resume reconstructs from checkpoint | `test_workflow_recovery`, `test_workflow_run_coordinator` recovery/resume suites |
| 16 | Stale checkpoint (completed output replaced between crash and resume) | completion identity (stat + digest) verified at the resume gate; mismatch re-executes the step | `test_workflow_run_coordinator` "resume re-executes a completed step whose output was replaced … (#750)" |
| 17 | Legacy checkpoint (pre-identity schema) | fail-conservative: unverifiable completed steps re-execute | resume-gate contract (crash-resume contract doc) |
| 18 | Remote source offline / never validated | benefit-of-the-doubt: never stale, never a wrong hit | `test_remote_source_cache` validator state machine (existing, unchanged) |
| 19 | Remote validator token change (ETag/Last-Modified) | `isStale` → registered asset flagged; display re-resolves | `test_remote_source_cache` FakeValidator suite (existing, unchanged) |
| 20 | Network timeouts/retries | bounded GDAL HTTP defaults (3 retries, 10 s connect, 30 s transfer), single source of truth | `configureRemoteCachingDefaults` consumed by the provider (Reliability 4.0 consolidation) |
| 21 | Cancellation during import scan | scan stops at the next batch boundary; partial tally + `cancelled=true` | `test_workspace_services` "ImportCenter cancel stops registration at a batch boundary" |
| 22 | Project switch while governance/cache services are active | cached governed document + v3-seen cleared at `clearProject`; no cross-project bleed | `test_workspace_project_v3` "Project switch never carries cached governed state" |
| 23 | Pool eviction racing shared content-addressed objects | digest-aware eviction keeps objects any live record references | `test_fault_injection` "eviction never deletes a shared content-addressed object" |
| 24 | Disk full (governance store) | every write path checks step + COMMIT and reports typed failures; callers surface the failure | covered-by-contract (row 6 mechanism); portable fault injection not available — accepted debt, documented in REVIEW_LOG |

## Scale modes

- **normal** — every suite runs at fixture scale by default.
- **100k** — `test_workspace_stress` (routine performance contract) and
  `test_workspace_catalog` "stays fast at 100k records". Baseline numbers are
  recorded in `.planning/data-runtime-governance-4/SCALE_BASELINE.md`.
- **1M** — opt-in stress only (`SICNU_WS3_STRESS=1`-style env gates); never
  materialized in GUI/service layers. Not part of this epic's routine runs.
