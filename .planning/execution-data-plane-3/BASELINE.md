# BASELINE — audited state at bf26b74a34 (master)

Findings from 5 parallel deep audits (jobs/workflow, workflow/checkpoint/GC,
fingerprint/cache, data/temporal/remote-COG, build/tests). file:line refs are at
master commit bf26b74a34.

## Execution spine (existing, keep-compatible)

- Facade `ExecutionPlane` (`src/processing/framework/execution_plane.h:192`) →
  `TaskCenter` (`task_center.h:155`, process-wide QObject singleton; admission,
  budget, priority, dispatch, cache consult/store, completion fan-out) →
  `JobEngine` (`src/jobs/job_engine.h:45`, Qt-free; raw std::thread worker pool,
  default workers = hw_concurrency-1, min 2; priority + exclusive jobs) →
  `RSOperator::run(params, RSOperatorContext&)` (`src/operators/framework/rs_operator.h:194`).
- Admission `processNextQueuedTasks` (`task_center.cpp:1107`): DAG parents gate →
  priority sort → global cap → per-profile cap → RSS watermark
  (`ResourceMonitor::memoryPressureHigh`) → RAM budget `TaskResourceBudget::canLaunch`
  (never-starve rule). `WaitingResource` status exists.
- Cancel: cooperative `shared_ptr<atomic<bool>>` + optional CancelHook
  (`job_engine.cpp:256-330`); TaskCenter cascades to DAG descendants, marshals
  QgsTask cancel via queued invoke (`task_center.cpp:1599-1642`).
- Results: `OutputCommitter::commit` = temp-validate → atomic rename → register
  asset + DerivationRecord (`src/processing/framework/output_committer.h:66`).
- Estimates: `RSOperator::executionEstimate()/estimateExecution()` (ADR 0117),
  `TaskResourceBudget` memory classes, `ResourceMonitor` RSS watermark 75%.
- Streaming today = declared memory policies + per-operator strip loops +
  `ChunkedProcessor` (algorithm-side, QtConcurrent bounded pool,
  `src/processing/algorithms/chunked_processor.h`). No framework tile runner.

## Workflow v2 (existing)

- `WorkflowRunCoordinator::startTrackedPipeline` (`workflow_run_coordinator.cpp:144`)
  = the only production path (GUI/MCP/agent/CLI). Builds `WorkflowRun` aggregate,
  flock run lock (`workflow_run_lock.cpp:97`, kernel-released on death), hands
  definition to `TaskCenter::submitPipeline` (Kahn topo, per-step tasks).
- Checkpoints: `~/.rs_studio/checkpoints/checkpoint_<runId>.json`, atomic
  tmp+fsync+rename+dir-fsync (`workflow_checkpoint.cpp:48-107`), written on every
  task transition; best-effort (save failure never aborts run).
- Startup recovery `recoverInterruptedRuns` (`workflow_checkpoint.cpp:187`) sweeps
  orphan tmp, reconciles active runs to Interrupted. Resume (`resumeRun`,
  `workflow_run_coordinator.cpp:404`): lock before read; completed steps skipped
  iff `outputLayerPath` exists (NO size/mtime validation — weakness W3); fresh
  submission swapped under original runId; ghost `_resume` checkpoint deleted after
  swap (crash window — W4).
- ArtifactGC (`artifact_gc.cpp`): finalize-only, Completed runs only; skips DAG
  leaves, declared artifacts, cacheHit steps, cache-protected paths, outside-workspace
  paths; rename to `.gctrash` then remove, cascades shapefile sidecars.
- Provenance: `DerivationRecord` attached via OutputCommitter; CLI registers
  checkpoint-served outputs before fresh ones; no-DataManager sidecar
  `<output>.provenance.json`.

## Execution identity & cache (existing)

- `ExecutionFingerprint` = SHA-256, contract v2, RFC-8785-style canonical params,
  hex-framed fields, inputs = `(assetId, revision)` stable-sorted + chained
  producer fingerprints; **destination excluded by key** (output vocabulary);
  unresolvable input ⇒ fail-closed uncacheable. Computed only in TaskCenter at
  submission, only for registered RSOperators with determinism opt-in
  (`deterministic==true` or grade `bit-exact`, ADR 0124).
- `ExecutionResultCache` (`execution_fingerprint.h:149`): **in-memory only**
  (explicit follow-up note `:148`); stores paths + size+mtime stats + result JSON;
  lookup validates size+mtime (self-healing erase); serve = same-dir temp copy +
  post-stage revalidation + rename; single-owner path claims; LRU 4096 entries.
- **No content digest anywhere** (`lazyContentDigest` hook exists, never set).
- Windows remove-then-rename non-atomic window acknowledged (`task_center.cpp:2705`).

## Data plane (existing)

- `DataManager` (`src/data/data_manager.h`): single-thread-affinity, no internal
  locks; storage = flat `QVector`s + linear scans (`findRecord` cpp:185);
  `assets()` deep-copies every match; `findByPath` does per-record
  `canonicalFilePath` stat storm (cpp:644); `derivedOutputsOf` O(N·inputs);
  no id/path/sourceKey indexes. In-memory only per session; host XML serializer
  (`src/app/data_project_serializer.cpp`) persists ProjectPersistent assets into
  `.qgz`.
- GUI `DataManagerPanel` = QTreeWidget full rebuild on refresh
  (`src/app/panels/data_manager_panel.cpp:714-795`), 250 ms coalesce; per-row
  `referenceCount` scan. No virtualization/pagination.
- Temporal workspace: `TemporalCollection` scene refs (path + optional
  assetId@revision binding), collection fingerprint = id@recordRevision +
  per-scene live revisions; path-only scene ⇒ uncacheable.
- Remote COG: `/vsicurl/` + SSRF-validated href (`stac_client.cpp:117`);
  HTTP defaults timeout 30/connect 10/retry 3 (`gdal_raster_source_provider.cpp:168`);
  **no ETag handling, no VSI/tile cache tuning, no app-level range cache**;
  network opens deferred off the app thread (`shouldDeferNetworkRasterOpen`).
- `gdal_runtime.cpp` = call_once GDALAllRegister only; no handle pooling.

## Build/test baseline (existing)

- CMake ≥3.20, C++20, Qt 6.8 system at /usr/lib, Ninja, ccache 4.13.6,
  SQLite3 REQUIRED (linked into qgis_core), Catch2 v3 FetchContent,
  ~298 test files, offscreen platform, `sicnu_discover_tests` PRE_TEST.
- Existing perf: `tests/test_perf_benchmarks.cpp` ([bench] lines, peakRSS),
  `scripts/benchmark_harness.py` (MCP-driven, JSON), `scripts/run_perf_baseline.sh`.
- Sandbox quirk: full session env breaks cmake prefix detection → all builds run
  via `scripts-safe-env.sh` wrapper (clean PATH/LANG).

## Baseline test status

To be filled after first local ctest run of the baseline tree
(see PERF_BASELINE.md / TEST_MATRIX.md).
