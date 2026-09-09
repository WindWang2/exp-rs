# BASELINE — audit @ origin/master c731e3e7 (2026-09-10)

## Repo / process state

- Local `master` was 1 behind; worktree branched from `origin/master` @
  `c731e3e7` (help QString fix). No open PRs, no open issues at start.
- Recently merged (dedupe check): #822 (issues #773–#817 sweep), #821
  (help/diagnostics), #820 (cartography), #819 **Workbench & Unified UX 6.0**
  — direct predecessor of this track, #818 (scientific data foundation).
- Parallel -7 branches (NOT mine, no file overlap so far):
  `feat/cloud-geospatial-io-7` (src/io + test_io_*), `feat/dataset-experiment-7`
  (planning only; will own dataset/experiment CORE — my §E stays UI thin
  client), `feat/execution-plane-runtime-7` + `feat/pi-spatial-scientist-harness-7`
  (both still at master HEAD). `feat/professional-workbench-7` was unclaimed —
  created fresh.
- Stray files in the MAIN worktree (someone else's sessions; untouched,
  not part of this branch): `AUDIT_DOSSIER_ISSUES_747_760.md`,
  `PROJECT_REVIEW_DOSSIER_5.0.md`, staged `.agents/ORIGINAL_REQUEST.md` edit.

## Existing seams this track consumes (verified by reading headers)

| Need | Authoritative seam | Notes |
|------|--------------------|-------|
| Bench lifecycle | `src/app/workbench/workbench_host.h` `IWorkbench` (isDirty/hasInFlightCompute/requestCancel/requestClose/saveState) | 6.0 #813; classification + georeferencer benches wired. **Gap A: shell closeEvent/quit never consults WorkbenchHost** (`main_window_misc.cpp:455` only `checkUnsavedChanges()`). |
| Inspector | `src/app/workbench/inspector_host.h` (`InspectorSection` lazy/cancel contract) | 6.0 sections: layer/vector/SAR. **Gap B: no provenance section** (6.0 FINAL_REPORT defers it, needs service injection). |
| Selection | `selection_context.h` snapshot + ContextRules | hasGovernanceSelection, asset/result ids available. |
| Task history | `src/processing/framework/task_center.h` `allTasks()`, `AlgorithmTaskInfo{source, parentTaskIds, resultPayload, params, status, start/end, outputLayerPath}` | RsJobPanel = live tree only (no query/retry/rerun/history states beyond finished). **Gap C.** |
| Provenance | `src/data/derivation_record.h` (`DerivationRecord`, `resolveInputLineage(ForParams)`), `src/experiment/lineage.h`, `data/governance/governance_store.h` | consumed read-only. |
| Temporal | `DataManager` TemporalCollection CRUD (`data_manager.h:257+`), `dialogs/temporal_analysis_dialog.h` (existing dialog), `core/qgsdataprovidertemporalcapabilities.h` | **Gap D: no workbench-level timeline/scene browser.** |
| Dataset/Experiment | `src/dataset/dataset_store.h`, `src/experiment/experiment_store.h` (+ leakage_audit, dataset_quality, reproduction_bundle) | **Gap E: no GUI surface at all.** |
| Model | `src/operators/framework/model_catalog.h`, `model_readiness.h`, `operators/runtime/model_runtime.h`, `model_execution_service.h` | **Gap F: dialogs exist (inference), no catalog/health bench.** |
| SchemaForm | `src/app/shell/schema_form_builder.*` 3.0 (units/recommended/visible-when/soft-min-max) | Gap G: warning explanations + a11y/keyboard depth. |
| Commands | `command_registry.h` + `command_defs.cpp` (408 lines) | Gap I: new benches must project into registry, not bypass. |

## 6.0 deferred items owned here

1. App-level quit wiring through WorkbenchHost hooks (explicit deferral note).
2. Provenance/processing-history inspector sections (explicit deferral note).
3. Per-widget persistence / IA normalization beyond registry projection.

## Test conventions

Catch2 in `tests/`, registered in `tests/CMakeLists.txt`
(`add_executable(test_x …)` + `sicnu_discover_tests`). Offscreen Qt tests;
worktree-local `build-dev` (dev-default preset), vcpkg deps shared from
`../exp-rs-win/build-win`. `test_task_center` full-sequence timing flakiness
on this host is documented pre-existing (6.0 A/B proof); run its cases in
isolation when touched.
