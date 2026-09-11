# OWNERSHIP — where this track's changes live

| Concern | Authoritative owner (unchanged) | This track touches |
|---|---|---|
| SAR geometry math | `src/processing/algorithms/sar/` (`sar_orbit`, `sar_terrain_geometry`, `sar_metadata`) | additive: `sar_geocoding.{h,cpp}`, `sar_temporal.{h,cpp}` |
| Operator surface | `RSOperatorRegistry` via `src/operators/rs/rs_operators_init.cpp` | additive `add(...)` rows + new operator files |
| Raster/vector I/O | `src/geospatial/**` (RasterReader, VectorReader), `src/processing/gdal` (GdalDatasetWrapper, streaming output) | consumed, not modified (unless an additive helper is proven missing) |
| Scientific contracts | `src/processing/contracts/scientific_contracts.h` | additive declarations only if needed |
| Help/catalog | `AlgorithmHelpCatalog` + `algorithm_meta` sidecars (drift-tested) | updated for every new operator |
| Docs | `docs/processing/sar-domain.md`, `temporal.md`, raster-vector doc (new) | additive sections |
| Tests | `tests/` (`sicnu_add_test`) | additive targets |
| Agent harness preflight | `src/agent/harness/**` (other track) | not modified |
| Workflow/scheduling | `WorkflowRunCoordinator/TaskCenter/JobEngine` | not modified |
| Model runtime | `IModelRuntime`/`runModelInference` | not modified |
| Rendering | QGIS canvas/layout | not modified |

## Shared-file edit policy

`rs_operators_init.cpp`, `tests/CMakeLists.txt`, `src/processing/CMakeLists.txt`,
and the operators CMakeLists are shared with concurrent 8.0 tracks. All edits
are single-line additions at the end of the respective registration/source
lists to minimize conflict surface. No reordering, no reformatting.
