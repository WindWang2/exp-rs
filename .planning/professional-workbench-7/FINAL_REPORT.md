# FINAL REPORT — Professional Remote Sensing Workbench 7.0

Branch `feat/professional-workbench-7` · base `origin/master` @ `c731e3e7` ·
worktree `../exp-rs-professional-workbench-7`.

## Delivered

1. **Shell shutdown/switch policy (§A)** — pure
   `WorkbenchShutdownFacts/ShutdownPlan` projection (`shutdown_policy.*`) +
   `QgisDesktopWindow::confirmWorkbenchShutdown` wired into quit,
   new/open-project. Order: dirty-bench `requestClose()` confirmations first
   (abort leaves everything untouched), then one in-flight confirmation that
   cancels via each bench's `requestCancel()` + `TaskCenter::cancelTask`
   (incl. `Cancelling` tasks). No silent drops, no waiting loops. The 6.0
   deferral is closed.
2. **Provenance Inspector section (§B)** — `ProvenanceSection` resolves
   asset/result/layer selections through DataManager + WorkspaceService and
   renders derivation records (operator/version, workflow/run/step or task
   reference, capped parameter snapshot, timestamps, fingerprint, cache
   truth), source assets with revisions, bounded ancestor chain (depth 6 +
   visited set, multi-branch marked truncated), derived outputs, governance
   verification, and quality warnings. Unknown renders as unknown.
3. **Unified Processing History (§C)** — `ProcessingHistoryModel/Panel`:
   one queryable projection over TaskCenter (source-tagged) +
   WorkflowRunCoordinator (incl. Interrupted/resumable), bounded cap with a
   truthful dropped counter, state filter + incremental search, actions
   through the real seams (cancel → cancelTask; rerun → retryTask with
   enqueue fallback; resume → resumeRun with surfaced failures; open/compare/
   inspect through shell signals).
4. **Temporal Workbench (§D)** — timeline strip (keyboard navigable, filter
   window band, hollow markers for unknown dates) + paged scene browser
   (200/page over the filtered set) + QA/cloud summary + preview/compare via
   shell seams. Metadata-only: no raster I/O in the UI; one index story
   between timeline and the filtered/paged table.
5. **Dataset/Experiment bench (§E)** — thin client over DatasetStore +
   ExperimentStore (user opens the DB files; existence-checked to avoid
   creating stores): dataset/version/sample-count/sample-preview/label-schema/
   split-manifest projections from the manifest document, runs table with
   surfaced first-page truncation, metric diff WITH dataset identity pins.
   Leakage/reproduction explicitly declared as CLI/Agent artifacts.
6. **Model bench (§F)** — catalog projection with runtime-evaluated readiness
   (`evaluateRuntimeReadiness` + verbatim reasons), catalog issues verbatim,
   manifest inspector, device FILTER (honestly labeled), test inference as a
   TaskCenter `rs:infer` task gated on readiness.
7. **UX consistency (§I)** — four registry commands (Ctrl+Shift+H/T/E/M) own
   their shortcuts; the 窗口 menu projects registry actions; icon aliases
   resolve; panels are real docks (addDockWidget, persisted with saveState).
   docs/ui-architecture.md Part III (§15–§20) documents every contract.

## Test evidence (Release, offscreen, sequential, -j2)

New: test_workbench_shutdown_policy 54/4 · test_provenance_section 61/6 ·
test_processing_history_model 42/5 · test_temporal_scene_model 16/3.
Regression (all green): inspector_host 27/6, selection_context 57/12,
command_registry 48/9, workbench_host 50/8, command_palette 23/4,
shortcut_conflicts 16/2 (caught a real Ctrl+Shift+D double-claim — fixed),
layer_sync_contract 23/7. `sicnu_geo_rs.exe` links clean; 100k-feed scale
cases inside the history/temporal model suites stay bounded.

## Adversarial review (M8) — 2 subagents (track maximum)

Architecture/UX: 3 P1, 8 P2, 8 P3. Concurrency/lifecycle: 0 P1, 5 P2, 7 P3.
ALL P1/P2 resolved in commit `214536a9` (+ shortcut fix `f3ec3f02`);
P3s fixed where cheap (vocabulary reuse, compare dedup, load-failure
feedback, null-safe item derefs, provenance disclosures, open() existence
guard) — full mapping in REVIEW_LOG.md.

## Known limitations / deferred (honest)

- §G SchemaForm deepening (units/ranges/a11y depth) and §H lazy thumbnails /
  cancellable stats: NOT started (paged/capped models and keyboard flow are
  done). Tracked as the next track's seed.
- WorkflowRunCoordinator holds `m_mutex` across checkpoint/GC file I/O;
  `runs()` on the GUI thread can therefore stall behind a finalizing run
  (review 2 finding 2). Core-side, owned by the execution-plane track —
  flagged for cross-task issue, not patched here.
- Dataset/experiment/model benches project first pages only (totals shown);
  full paging controls are a small follow-up.
- TaskCenter history depth is session-scoped by its own retention policy;
  the panel labels this truthfully and never fabricates persistence.
- Sibling tracks active during this one: cloud-geospatial-io-7 (src/io),
  dataset-experiment-7 (dataset/experiment core), execution-plane-runtime-7,
  pi-spatial-scientist-harness-7, model-runtime-multimodal-7. No file
  overlaps observed; integration point: if DatasetStore gains GUI-oriented
  open conventions, the bench's path-based opener should adopt them.
