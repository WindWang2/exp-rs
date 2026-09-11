# GOAL — Professional Remote Sensing Workbench & Unified UX 8.0

Branch `feat/professional-workbench-8` · worktree `../exp-rs-professional-workbench-8` ·
base `origin/master` @ `2d4f0daedd` (execution-time latest; PR #836 merged).

## Mission

Evolve the desktop application into a coherent, scalable professional
remote-sensing workbench in which Data, Layers, Processing, Models, Datasets,
Experiments, Harness, Maps, Results, and Provenance share one consistent
selection/command/task lifecycle, with schema-driven forms and large-scale
metadata UX.

## Scope (track-specific work packages)

- **A. End-to-end UX architecture audit** — mapped in BASELINE.md /
  ARCHITECTURE.md; findings drive the milestone plan.
- **B. SchemaForm 4.0** — deepening the schema-driven form contract
  (nested objects, arrays, dynamic enums, async checks, a11y, round-trips).
  This was explicitly deferred by track 7.0 ("the next track's seed").
- **C. Context and command state** — SelectionContext/ContextRules/
  CommandRegistry already own availability + why-disabled (5.0/7.0);
  deepen with in-flight-task/dataset facts and a suggested-next-action
  projection.
- **D. Large metadata UI** — 100k+ logical records: model/view with lazy
  fetch, filter, bounded retained rows, truthful truncation.
- **E. Lazy preview/thumbnail/statistics services** — bounded asynchronous
  raster thumbnails + vector previews on the EXISTING bounded analysis pool
  (#797), generation/cancellation semantics, never blocking the GUI.
- **F. Unified professional workspaces** — WorkbenchHost 5.0 already owns
  the workspace model; audit only, extend only where a verified gap exists.
- **G. Task/result/provenance UX** — RsJobPanel + ProcessingHistoryPanel +
  ProvenanceSection already cover the lifecycle; strengthen tests/links
  where verified gaps exist.
- **H. UI consistency and accessibility** — applied inside B/E/D deliverables
  (accessible names/descriptions, design tokens, keyboard navigation).
- **I. Interaction robustness** — stress tests for every new async seam
  (teardown, selection churn, project switch, layer deletion).

## Non-negotiable invariants (carried from the goal)

- Pi is the single agent loop; no second agent framework.
- Workflow execution: WorkflowRunCoordinator → TaskCenter → JobEngine →
  Executor/RSOperator; no parallel scheduler.
- RSOperatorRegistry stays the authoritative operator surface.
- Model execution stays behind IModelRuntime / runModelInference.
- QGIS stays the only map/render engine; previews are bounded GDAL reads +
  QImage composition for catalog-size views, never a second renderer.
- Persistence stays DatasetStore/ExperimentStore/DataManager; extend only.
- Geospatial I/O stays in src/geospatial/**; the preview service consumes
  RasterReader (readWindowResampled + Nearest overview policy) — the seam
  explicitly documented for "Preview/UI surfaces".
- Command availability converges on CommandRegistry/ContextRules/
  SelectionContext.
- New registries/managers require proof that no equivalent exists — the
  preview service reuses HistogramWidget::analysisThreadPool() rather than
  creating a second pool.

## Definition of done

See the goal contract: baseline audit recorded; verified gaps implemented or
refused with rationale; targeted builds green; tests pass with honest
classification; deterministic bounded tests; scale evidence; docs
synchronized; adversarial review remediated; no unrelated churn; branch
pushed; PR created. Online CI/CD not required and not waited on.
