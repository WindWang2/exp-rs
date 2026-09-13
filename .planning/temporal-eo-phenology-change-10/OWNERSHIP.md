# OWNERSHIP — temporal-eo-phenology-change-10

## This track owns (write scope)

- `src/processing/algorithms/temporal/` — temporal kernels (new files
  `temporal_calendar.*`, `temporal_change.*`, `temporal_region_table.*`;
  additive extensions to `temporal_fit.h/.cpp`, `temporal_gapfill.h/.cpp`
  only where a shared contract demands it).
- `src/operators/rs/rs_temporal_*` — temporal operators: new
  `rs_temporal_regularize_operator.*`, `rs_temporal_harmonic_breaks_operator.*`,
  `rs_temporal_extract_regions_operator.*`, `rs_temporal_region_features_operator.*`;
  additive schema params for `rs_temporal_monitor_operator.*` (T-1),
  `rs_temporal_smooth_operator.*` (whittaker_robust),
  `rs_temporal_phenology_operator.*` (multi-cycle).
- `src/operators/rs/rs_operators_init.cpp` — append-only registration lines.
- `src/operators/CMakeLists.txt` + `src/processing/CMakeLists.txt` —
  append-only source list entries.
- `tests/` — new temporal test files + append-only `tests/CMakeLists.txt`.
- `docs/processing/temporal.md` (per-operator contract table),
  `docs/temporal/ARCHITECTURE_V3.md` (new), ADR file for the platform
  increment, `CHANGELOG.md` (append entry).
- `data/processing/algorithm_meta/rs-temporal-*.json` (new sidecars),
  `data/agent/knowledge/` temporal knowledge files (append-only).

## Explicitly NOT owned (read-only; owners)

| Area | Owner track / authority |
|---|---|
| `src/core/**`, `src/gui/**`, `src/app/**` | workbench-9 / vendored QGIS |
| `src/processing/algorithms/sar/**`, `src/operators/rs/rs_sar_*` | advanced-sar-polsar-insar-10 |
| `src/geospatial/**`, remote IO / range cache / EO cubes | cloud-data-fabric-datacube-10 |
| `src/dataset/**`, `src/experiment/**` | dataset-experiment-mlops lineage |
| `src/operators/runtime/**` (model runtime) | model-runtime platform |
| `data/processing/toolbox_manifest.json` (provider algorithms) | processing platform |

## Shared-file conflict protocol

- `rs_operators_init.cpp`, CMakeLists, knowledge indexes: **append-only
  narrow edits**, each in their own integration commit so concurrent 10.0
  tracks rebase cleanly.
- `CHANGELOG.md`: single appended section for this track.
- If another track's commit lands first on a shared registry line, rebase and
  re-verify targeted temporal tests (never merge their logic).
