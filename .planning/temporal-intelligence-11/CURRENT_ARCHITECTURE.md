# CURRENT_ARCHITECTURE — temporal capability on origin/master `a5b11b7f`

## Authority map

| Concern | Authority | Notes |
|---|---|---|
| Temporal kernels (10.0 lineage) | `src/processing/algorithms/temporal/*` (`sicnu::temporal`) | fit/change/calendar/stream/region_table/preflight/monitoring/gapfill/time/workspace/collection/contracts/band_roles/stac_adapter/stats |
| D16 hermetic library | `SICNU_TEMPORAL_TIMELINE_SOURCES` (`sicnu::temporal::d16`) | cube/smoothing/phenology_metrics/breakpoint_detection/trend_analysis/spatiotemporal_filter; qgis-free; **library-only**, unwired |
| Operator registration | `REGISTER_RS_OPERATOR` in `src/operators/rs/rs_operators_init.cpp` (~173-215); sources in `src/operators/CMakeLists.txt` (explicit list 168-192) | auto-surfaces to CLI/MCP/agent schemas |
| Shared run prologue | `temporal_input::prepareTemporalRun` (`rs_temporal_collection_input.*`) | collection parse → preflight → band resolution |
| NoData/validity normalization | `TemporalTileReader` (`temporal_stream.*`) — sole point | NaN collapse incl. QA_PIXEL/SCL |
| Point-in-polygon membership | `temporal_region_table::buildRegionGeometry` | pixel-center even-odd, rotation rejected |
| Linear algebra | `temporal_linalg_detail.h::solveSmallDense` | Gaussian elim + partial pivot, singular iff pivot<1e-12, vectors **by value** |
| Time semantics | `temporal_time` (ISO-8601/DOY/filename), t = days from collection epoch; harmonic period exactly 365.25 d | date-only strings never promoted |
| Capability knowledge | `data/agent/capabilities/*.json` + `src/agent/harness/capability_catalog.cpp` (guard-tested) + `data/processing/algorithm_meta/*.json` | failure codes closed vocabulary `kFailureModeCodes` |
| Docs contract | `docs/processing/temporal.md` per-operator row policy (ADR 0148) | "BFAST/CCDC-inspired" wording locked |
| Workspace identity | `temporal_workspace` fingerprinting (ADR 0125) | fail-closed incl. remote/VSI |
| Tests | Catch2; independent-truth culture; `TestScene/writeTestScene` GTiff fixtures | bit-exact determinism anchors exist |

## Seams this track uses (append-only)

1. `src/processing/CMakeLists.txt` temporal block — new kernel files.
2. `src/operators/CMakeLists.txt` + `rs_operators_init.cpp` — 3 new operators.
3. `tests/CMakeLists.txt` — new test executables.
4. `data/agent/capabilities/temporal.json` (+ catalog if guard requires).
5. `docs/processing/temporal.md` rows + `docs/temporal/ARCHITECTURE_V3.md` note + CHANGELOG.
6. `src/app/dialogs/temporal_analysis_dialog.{h,cpp}` `kAlgorithms[]` + pages.
7. `src/agent/spatial_tools/spatial_tool.cpp::registerBuiltinTools` — 1 new tool.

## Known tensions (recorded, not fixed here)

- 3 Mann-Kendall implementations; 2 BFAST-like kernels (10.0 wired / D16 library-only).
- `solveSmallDense` copies by value; per-fit Gram allocation (G refactor target, bit-exact order preserved).
- `capability_catalog.cpp` familyMap vs `capabilities/temporal.json` counts disagree (14 vs 17) — verify guard semantics before editing.
