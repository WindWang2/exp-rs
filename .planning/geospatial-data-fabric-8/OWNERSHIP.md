# OWNERSHIP — authority map (post-baseline)

Laws preserved; nothing in 8.0 forks an authority.

| Concern | Authoritative seam | 8.0 relationship |
|---|---|---|
| Agent loop | Pi | untouched |
| Workflow execution | WorkflowRunCoordinator → TaskCenter → JobEngine → Executor/RSOperator | untouched (only fingerprint input identity extended) |
| Operator surface | RSOperatorRegistry | `io:*` additions register through it |
| Model execution | IModelRuntime / runModelInference | untouched |
| Map rendering | QGIS | untouched |
| Dataset/experiment persistence | DatasetStore / ExperimentStore | untouched |
| Geospatial I/O | `src/geospatial/**` (target `sicnu_geospatial`, Qt-free) | all new I/O lives here |
| Publication/provenance | governed output/committer seams (VectorWriter/atomic_fs, output_committer) | GeoParquet writes go through VectorWriter |
| Command/UX state | CommandRegistry / ContextRules / SelectionContext | untouched (headless CLI only) |
| Remote identity | `RemoteSourceValidator` / `RemoteSourceIdentity` | enriched additively |
| Execution identity | `sicnu::data::execution_identity_resolver` | activated (collector + host install); geospatial token provider added |
| Cache taxonomy | ADR 0139 (GDAL VSI owned; `/vsirangecache/` is the one new handler) | unchanged |
| URI identity/redaction | `ResourceUri` (ADR 0135) | reused, never bypassed |

## Files 8.0 intends to touch (shared-file minimization)

- `src/geospatial/**` — main implementation surface (new files + additive edits).
- `src/data/execution_identity_resolver.*` + one new bridge file — seam activation.
- `src/processing/algorithms/temporal/temporal_workspace.cpp` — consult the installed
  resolver when an unregistered remote input cannot resolve (two narrow call sites).
- `src/app/main.cpp`, `src/cli/main_cli.cpp` (+ worker/pipeline runner if applicable) —
  one install call each at startup.
- `tests/CMakeLists.txt` — appended test targets only.
- `docs/io/**`, `CHANGELOG.md` — documentation ledger.
