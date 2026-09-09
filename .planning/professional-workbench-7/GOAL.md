# GOAL — Professional Remote Sensing Workbench 7.0

Branch: `feat/professional-workbench-7` · Worktree: `../exp-rs-professional-workbench-7` · Base: `origin/master` @ `c731e3e7`

## Mission

Make the GUI the unified professional entry point for Dataset, Model, Workflow,
Harness, Map and Experiment work — one workbench, not a collection of
independent dialogs and duplicated tools. Consume the 6.0 seams (IWorkbench
lifecycle hooks, InspectorHost sections, SelectionContext, CommandRegistry,
SchemaFormBuilder) instead of adding parallel surfaces.

## Hard constraints

- master read-only; work only in this worktree. ≤ 2 subagents TOTAL (reserved
  for final adversarial review). No online CI dependency.
- Thin client only: GUI never re-implements algorithms or owns a second
  data store. TaskCenter/JobEngine/Workflow, DataManager/GovernanceStore,
  DatasetStore/ExperimentStore, ModelRuntime/ModelCatalog, QGIS canvas remain
  authoritative.
- Resource-bounded local validation: `-j2` build (heavy deps `-j1`),
  `CTEST_PARALLEL_LEVEL=1`, offscreen Qt tests, shared vcpkg install from
  `../exp-rs-win/build-win`.
- FAIL stays FAIL; unverifiable → warning/unknown. No fabricated success.

## Track sections (from the goal brief)

A. App/Workbench lifecycle — quit/project-switch/external-close consume
   dirty/in-flight/cancel/save/requestClose hooks.
B. Provenance Inspector — source assets, dataset version, workflow/run,
   operator/model, params, timestamps, derivation chain, verification,
   experiment, quality warnings.
C. Processing History — one queryable projection of TaskCenter/Workflow/
   Harness/manual processing: states, inputs/outputs, retry/resume, open
   result, inspect provenance, rerun, compare.
D. Temporal Workbench — timeline/scene browser/filtering/timestep/preview/
   chart/QA/compare over DataManager TemporalCollections, paginated.
E. Dataset/Experiment Workbench — thin client over DatasetStore/
   ExperimentStore: versions, samples, labels, splits, leakage, stats,
   experiments, compare, reproduction readiness.
F. Model Workbench — catalog/manifest/backend/device/compat/health/memory/
   test-inference over ModelCatalog + ModelRuntime seams.
G. Inspector/SchemaForm deepening — processing/provenance sections, units,
   ranges, warning explanations, conditional groups, a11y, keyboard.
H. Large Data UI — virtual/paged models, lazy thumbnails, incremental
   search, cancellable stats, bounded caches for 100k+ rows.
I. UX consistency — command enablement, context rules, mode transitions,
   icon/text, panel visibility, empty/error/loading states, shortcuts.

## Completion definition

GUI is a coherent professional entry; review P0/P1 zero, reasonable P2 fixed;
PR merged description covers architecture/tests/limits.
