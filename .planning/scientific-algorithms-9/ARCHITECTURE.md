# ARCHITECTURE — Scientific Algorithms 9.0

## Invariants (unchanged from 8.0, enforced)

- Pi is the only agent loop/runtime; no second scheduler, renderer, I/O
  authority, or store is introduced anywhere in this track.
- Execution chain `WorkflowRunCoordinator → TaskCenter → JobEngine →
  Executor/Operator` is consumed, never modified.
- QGIS stays the only map/layout rendering authority (no kernels touch it).
- `src/geospatial` stays the geospatial I/O authority; processing consumes
  it via `src/processing/gdal` wrappers (`GdalDatasetWrapper`,
  `GdalBlockStream`, `GdalStreamingOutput`) — the established seams.
- Kernels never reach into UI/agent layers; operators translate JSON
  parameters → options structs; kernels are pure (no GDAL in unit-testable
  free functions where avoidable, GDAL only at the streaming boundary).

## New architectural element (M1): unified numeric semantics seam

Problem evidence: SAR terrain averaged anisotropic cell sizes (#855); the
spectral-index probe invented hardcoded sentinels `-9999.0f`/`65535.0f`
(#856 dead block); contracts accepted +Inf scales (#873); flood fill's
boundary notion ignored NoData (#848). Each is a case of a kernel or
operator re-deriving raster semantics locally.

Design (conservative, additive):

1. `src/processing/contracts/` grows the single vocabulary:
   - finite-scale validation lives in `domainFromDeclaredScale` (fix #873),
   - scale probing is defined once (positive-only observation — fix #856),
   - per-band NoData handling is expressed through GDAL band attributes and
     `bandNoDataSentinel` (no hardcoded sentinels anywhere).
2. Kernels take **explicit anisotropic geometry** (`cellSizeX/Y` or
   geotransform-derived) — never an averaged scalar (fix #855; matches the
   existing `terrain_analysis` convention).
3. Hydrology boundary = "valid cell adjacent to the raster perimeter OR to
   NoData" (fix #848), documented in `terrain_flow.h`.
4. Multi-band streaming outputs set NoData **per band** (fix #854) via the
   existing `GdalStreamingOutput::setBandNoDataValue`.
5. `tests/test_semantic_drift_9.cpp` mechanically greps owned
   kernels/operators for the banned patterns (invented sentinels, averaged
   cell sizes, dataset-wide NoData on typed outputs) so the drift cannot
   silently return.

No interface breaks: all fixes are signature extensions with default-free
call-site updates confined to files this track owns.

## Milestone → code mapping

| Milestone | Primary files |
|---|---|
| M0 | the six defect sites + regression tests |
| M1 | `scientific_contracts.{h,cpp}`, `nodata_utils.*`, drift test |
| M2 | `sar/sar_terrain.*`, `rs_sar_terrain_flatten_operator.cpp`, `test_sar_kernels` |
| M4 | `rs_spectral_index_operator.cpp`, `test_spectral_*` |
| M6 | `terrain_flow.*`, `terrain_analysis.*` (audit only — already anisotropic), `test_terrain_foundation5` |
| M9 | corpus extension across `tests/test_*` (analytic fixtures) |

M3/M5/M7/M8 are audit-and-evidence milestones over already-merged 8.0/7.0
capability (see CAPABILITY_MATRIX.md): they extend known-answer coverage and
resource-bound documentation where a verified gap exists, and refuse
re-implementation otherwise.
