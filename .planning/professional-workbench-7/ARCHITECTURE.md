# ARCHITECTURE — Workbench 7.0

## Authority map (unchanged, enforced)

- Map/layers/layout: vendored QGIS. No second rendering engine.
- Execution: TaskCenter → JobEngine (+ WorkflowRunCoordinator for pipelines,
  Harness for agent runs). GUI submits and observes; never executes.
- Data identity/provenance: DataManager + GovernanceStore + DerivationRecord.
- ML data: DatasetStore / ExperimentStore / leakage audit / reproduction
  bundles (core owned by `feat/dataset-experiment-7`; GUI reads only).
- Models: ModelCatalog manifests + ModelRuntime/ModelExecutionService.

## What this track adds (all under `src/app/`, thin)

1. **Shell lifecycle policy (A)** — `main_window` closeEvent/quit and project
   switch route through `WorkbenchHost`: collect dirty benches → save/cancel
   confirmation; collect in-flight benches → cancel confirmation (via
   `requestCancel()`); external windows via their benches' `requestClose()`.
   One policy object (`workbench_shutdown_policy`) so it is unit-testable
   without windows. Bounded: one pass, no retry loops.
2. **Provenance section (B)** — new `ProvenanceSection : InspectorSection`
   registered by the shell. Resolves the selection (layer → source path →
   derivation records; result id → governance entity; asset id) and renders:
   source assets, workflow/run, operator, params, timestamps, derivation
   chain (parent task ids / derivation records), verification status, quality
   warnings. Read-only over GovernanceStore/DataManager. Unknown → "未知".
3. **Processing History (C)** — new `ProcessingHistoryModel` (QAbstractTableModel,
   virtual: rows come from a bounded TaskCenter projection + optional on-disk
   run journal if one exists — otherwise session-scoped, truthfully labeled)
   + `ProcessingHistoryPanel` dock (filter by state/source, incremental
   search, retry/rerun/open-result/inspect-provenance/compare actions routed
   through TaskCenter re-submission — same command seam, no second executor).
   Large-data rules from §H apply (no widget-per-row, lazy, bounded).
4. **Temporal Workbench (D)** — `TemporalWorkbenchPanel`: collection picker
   (DataManager records), paginated scene/date browser (paged model, lazy
   thumbnails), current timestep selection driving canvas preview, date
   filtering, QA/status column, compare-two-dates action launching the
   existing comparison surface. No new catalog store.
5. **Dataset/Experiment bench (E)** — `DatasetWorkbenchPanel` +
   `ExperimentWorkbenchPanel` thin clients: version list, samples (paged
   model), label schema, split summary, leakage audit display, stats,
   experiment runs, run compare, reproduction readiness badge. All via the
   stores' public APIs; zero local persistence.
6. **Model bench (F)** — `ModelWorkbenchPanel`: catalog projection (paged),
   manifest detail, backend/device info, compatibility + readiness
   (model_readiness), memory estimate, test-inference action through the
   Model Runtime seam, errors shown verbatim.
7. **Inspector/SchemaForm + large-data + UX (G/H/I)** — warning explanation
   rendering, keyboard navigation on section/tab switch, paged-model shared
   helper (`paginated_table_model`), and a UX consistency pass projecting new
   actions into CommandRegistry with deterministic ContextRules reasons.

## Explicit non-goals

- No second scheduler/executor/queue; no background "history daemon".
- No new persistence for GUI state beyond QSettings keys already conventioned
  (`workbench/<id>/state`, dock geometry).
- No dataset/experiment core changes (owned by the sibling track; if their
  store API is missing something UI-shape-level, we adapt in the GUI layer
  and record the integration point).
- No decorative restyle.

## Risks

- TaskCenter history depth: `allTasks()` may be session-only. The panel must
  label what it truly has (session vs persisted) — never fake persistence.
- Sibling tracks touching TaskCenter/Workflow: consume via header APIs only;
  final integration sync before PR.
