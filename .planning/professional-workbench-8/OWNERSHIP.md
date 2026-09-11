# OWNERSHIP — Professional Workbench 8.0

## Files this track owns (new)

- `src/app/shell/schema_form_builder.*` — extended in place (3.0 → 4.0).
- `src/app/preview/asset_preview_service.*` — NEW: bounded async raster
  thumbnail / vector preview service (single owner of catalog previews).
- `src/app/workbench/asset_catalog_model.*` — NEW: lazy-fetch model over
  DataManager snapshots for the data manager panel.
- Tests: `tests/test_schema_form_4.cpp`, `tests/test_asset_preview_service.cpp`,
  `tests/test_asset_catalog_model.cpp`, `tests/test_context_facts_8.cpp`
  (names may drift slightly; the test matrix records final names).
- Planning docs under `.planning/professional-workbench-8/`.

## Files this track modifies (existing, minimal-diff discipline)

- `src/app/panels/data_manager_panel.*` — swap tree internals to the catalog
  model; public API (rowText/selectedAssetId/signals) preserved.
- `src/app/workbench/selection_context.*` — additive ContextFacts fields +
  suggested-next-action projection.
- `src/app/CMakeLists.txt`, `tests/CMakeLists.txt` — additive target/source
  registrations only.
- `docs/ui-architecture.md` — contract documentation for every new seam.
- `CHANGELOG.md` — one entry.

## Files this track must NOT touch (concurrent 8.0 tracks / authority)

- `src/processing/framework/**` (execution-plane-8) — TaskCenter consumed
  read-only.
- `src/geospatial/**` internals (geospatial-data-fabric-8) — RasterReader
  consumed as-is.
- `src/operators/**` (model-runtime-8, scientific-processing-8) — operator
  kernels and model runtime off-limits.
- `src/experiment/**`, `src/dataset/**` cores (dataset-experiment-mlops-8).
- `src/agent/**` (mlops-8 edits mcp_server etc.).
- `pi/**`, vendored QGIS subset beyond additive widget files.

## Shared-file risk ledger

| File | Also touched by (known) | Mitigation |
|------|--------------------------|------------|
| tests/CMakeLists.txt | every track | pure appends at section boundaries |
| src/app/CMakeLists.txt | desktop-UX tracks (older, merged) | appends |
| CHANGELOG.md | every track | prepend entry, rebase late |
| docs/ui-architecture.md | workbench tracks only | mine |
