# REVIEW_LOG

## Round 0 — issue triage (2026-09-06)

- Verified open issues at HEAD 58eb196baa; mapped #746/#751/#752→A, #758(1-4)→B,
  #758(5-7)→C, #749→D, #750→E, #754→F, #753→G.
- Out of scope: #747/#748/#755/#756/#757 (plugin-sdk epic), #759 (algorithms epic),
  #760 (docs — addressed by this epic's PROJECT.md refresh).

(subsequent review rounds appended below)

## Round 1 — self-review notes (2026-09-06, while adversarial reviewers run)

- Verified MCP surface does NOT bind the run-state observer: it reads run
  state through `sicnu::agent::setWorkflowRunsProvider` directly from the
  coordinator (src/app/main.cpp:229-239), which is authoritative. The
  single-observer slot therefore stays owned by ProjectContext (GUI/headless)
  — deliberate, documented here.
- Test evidence accumulated (all QT_QPA_PLATFORM=offscreen, sequential):
  governance_store 102a/9c, workspace_project_v3 103a/6c, workspace_services
  183a/14c, fault_injection 152a/8c, workflow_run_coordinator 175a/10c,
  resume_provenance 67a/2c, cache_e2e 189a/12c, artifact_store 85a/9c,
  governance_tools 71a/1c, workflow_recovery 87a/9c, incremental_cache
  24a/6c, temporal_workspace 253a/19c, workspace_stress 20a/1c,
  data_project_roundtrip 288a/13c, workspace_catalog 254a/4c,
  worker_host 28a/8c, remote_source_cache 29a/5c, workflow_cancel 3a/1c,
  workflow_pipeline 27a/3c, workflow_execution_plane 36a/6c,
  workflow_runtime 223a/21c, workflow_session_controller 17a/4c,
  exprs_workflow_schema 15a/5c — all passing on the branch.
- Machine-load flake observed once: test_workspace_catalog page-timing
  (504.9ms vs 500ms) while two epics' builds ran concurrently; passed on
  rerun. Perf suites must be judged on a quiet machine (final pass).

## Round 1 — adversarial review (4 independent reviewers, 6 lenses)

Reviewers: (1) architecture/layering/duplication, (2) correctness/scientific
semantics/data integrity, (3) concurrency/lifetime/cancellation + performance,
(4) API contract/cross-frontend + docs-vs-code claims. Consolidated triage:

### Fixed (P0/P1)

- P0 Untracked new files (pool .h/.cpp, ADR 0130) — branch was not buildable
  from a clean checkout. Added.
- P1 Pool-tier cache lookups validated only object digests, never the
  recorded input stats — a restart could serve a result computed from foreign
  input bytes. `lookupExecution` now re-validators input size+mtime before
  serving a pooled entry.
- P1 `checkpointForBackup` could not detect a blocked checkpoint
  (sqlite3_exec discards the busy row; PASSIVE fallback completes zero frames
  under contention) — snapshot refusal was effectively dead code. Now steps
  the TRUNCATE statement and requires busy == 0; PASSIVE fallback removed;
  -shm sidecar copy dropped; FAULT_MATRIX rows 4/5 rewritten.
- P1 Resume-gate digest verification failed OPEN (digest error or a lowered
  budget skipped verification). A recorded digest now must re-verify or the
  step re-executes.
- P1 Run-state mirror clobbered run rows (startedMs zeroed; definition/
  summary/metadata/tags wiped). recordRun now merges with the stored row;
  the coordinator derives truthful start stamps from the run creation time.
- P1 Cross-project bleed via a SUCCESSFUL openStore of a different path
  (Save-As). Cache + v3Seen now clear on every path change.
- P1 `SICNU_DATA_WATCH_LIMIT` documented but never read. Implemented.
- P1 Restore diagnostics surfaced nowhere user-visible. GUI now shows a
  status message + qWarning for non-fatal governance diagnostics; CLI prints
  them to stderr.
- P1 Truthful run states had zero tests. Added a service-level suite (state
  transitions, startedMs/definition/tags preservation, Unknown anchors,
  Failed-run orphan classification).
- P1 Worker pool destructor raced in-flight runs (UAF) and QProcess
  cross-thread use was possible. Added in-flight drain on destruction and an
  owner-thread contract enforced at run(); a result racing a cancel is now
  reported as cancelled, never as success.

### Fixed (P2 selection)

- `m_taskRegisteredInputStats` leaked at every failed/canceled terminal
  site — cleanup added to all of them.
- `clearAll`/`clearDocumentEntities` could commit a partial clear — every
  per-table DELETE is checked with rollback.
- Every writer now checks BEGIN (16 sites) — a failed BEGIN no longer leaves
  rows committing in autocommit; `removeSmartCollection` step/commit checked.
- `quick_check` per save throttled (2 s positive window; failures never
  cached).
- Completion digests computed OUTSIDE the coordinator mutex; the missed-
  window fold stamps stat-only (documented).
- Run-state mirror bound with Qt::QueuedConnection (store writes no longer
  under the coordinator mutex); observer converted from a raw std::function
  slot to a lifetime-managed Qt signal with the WorkspaceService as
  connection context.
- Watcher: addPath results respected (in-place writes keep the watch; only
  replacements drop it), failed re-arms get bounded 1 s retries and the
  entry is dropped after ~1 min; change notifications deferred off the
  QFileSystemWatcher emission.
- Alias-owner lookups guard against null cached statements; the shared
  orphan-state predicate is a single macro; serializer emits
  `workspace.stale_document_repersisted` and `markV3Seen()` covers
  missing/unparseable v3 blocks; CLI/GUI surfaces above.

### Accepted debt (documented, with rationale)

- Corrupt-while-open integrity detection is best-effort (SQLite page cache
  may serve a small corrupted DB). Cache-on-read + checked writes carry the
  correctness load; FAULT_MATRIX row 2 states this.
- Second-process writer between snapshot checkpoint and copy is a residual
  race window (FAULT_MATRIX row 5). Closing it fully requires the sqlite
  backup API — follow-up.
- `LocalWorkerPool` ships as a tested, unwired seam (ADR 0130 decision 9).
- Resume validates only each step's own output identity; re-executed
  producers do not invalidate already-served downstream steps (safe only
  under the determinism gate that governs caching — inherent to the
  checkpoint design, documented).
- Fingerprint input digests are computed under the TaskCenter lock
  convention (bounded by SICNU_CACHE_INPUT_DIGEST_MAX_MB); moving the
  hashing off-lock is a larger refactor deferred as debt.
- Env-budget parse helpers duplicated across layers (SICNU_CACHE_* /
  SICNU_RESUME_*): deliberate per-layer knobs, not consolidated.
- Grouped executions (no declared output) and sidecar-writing steps always
  re-execute on resume (conservative-but-expensive), inherent to stat
  identity on the declared path.

### Round-1 verification (post-fix)

- Root-caused one test regression the review fixes exposed: a PRE-EXISTING
  race in RsPipelineRunner's resume wait (TaskCenter's pipeline roll-up can
  beat the coordinator's queued fold; the runner then reported a mid-fold
  aggregate with the final step still "Running"). Fixed in the runner by
  draining the coordinator aggregate to all-terminal (bounded) before
  reporting.
- Also fixed en route: storeExecutionResultLocked removed the registered-
  input stat bindings before reading them (sweep ordering bug).
- Final targeted run (all sequential, offscreen): 22 suites / 2427 assertions
  - all passing; 100k stress re-run green.
