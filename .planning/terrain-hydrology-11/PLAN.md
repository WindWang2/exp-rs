# PLAN — terrain-hydrology-11

Baseline: origin/master @ `a5b11b7f10` (2026-09-15). Worktree:
`../exp-rs-terrain-hydrology-11`, branch `zcode/terrain-hydrology-11`.

## Architecture shape (extends master's seams, no second authority)

- New kernels live beside the existing terrain family in
  `src/processing/algorithms/terrain_*.cpp/h`, same contract style as
  `terrain_flow.h`: free functions / small namespaces, plain float frames,
  explicit nodata, deterministic tie-breaks documented in headers.
- Operator surface: extend `rs:terrain_flow` (hydrology products) and
  `rs:terrain_analysis` (landform products); add `rs:terrain_viewshed` and
  `rs:terrain_solar` operators (new registration + capability sidecars).
  Cancellation via `context.throwIfCancelled()`, memory estimates per existing
  metadata conventions, sidecars regenerated with `capability_knowledge_tool gen-meta`.
- UI: extend `src/app/dialogs/terrain_dialog.*` (hydrology/visibility products).
- Agent: `spatial:terrain_profile` + `spatial:terrain_viewshed_inspect` tools in
  new `src/agent/spatial_tools/terrain_spatial_tools.*`, registered append-only.
- Truth: `tests/synthetic_terrain_dem.h` closed-form DEM factory; every kernel
  tested against independent closed-form values, never against the implementation.

## Phases → commits

| Phase | Content | Files |
|---|---|---|
| 0 | audit + planning artifacts | `.planning/terrain-hydrology-11/*`, `.gitignore` |
| 1 | synthetic DEM truth (H) + hydrology kernels A/B/C: flat resolution, D∞, streams/Strahler, outlets | `tests/synthetic_terrain_dem.h`, `terrain_hydrology.*`, `tests/test_terrain_hydrology.cpp`, processing CMake |
| 2 | viewshed/horizon (D) | `terrain_viewshed.*`, `tests/test_terrain_viewshed.cpp` |
| 3 | solar (E) + landform (F) | `terrain_solar.*`, `terrain_landform.*`, `tests/test_terrain_solar.cpp`, `tests/test_terrain_landform.cpp` |
| 4 | operator + sidecar + UI + agent surface (G) | `rs_terrain_*_operator.*`, capability sidecars, terrain_dialog, agent tools, docs, README, CHANGELOG |
| 5 | scale/cancel/failure hardening | size guards, cancel hooks, FAILURE matrix tests |
| 6 | E2E + known-answer sweep | `tests/test_terrain_analytics_e2e.cpp`, TEST_MATRIX full pass |
| 7 | adversarial review (subagent #2) + P0/P1 fixes | REVIEW_LOG.md |
| 8 | double verification, rebase, push, PR | PR_BODY.md |

## Verification strategy

- Each kernel: Catch2 known-answer tests with independent closed-form truth +
  at least one negative/edge test (NoData, out-of-range params, empty grid).
- Operators: E2E through the registry like `test_terrain_foundation5.cpp` does
  (synthetic raster file → operator run → read output → compare).
- Determinism: identical re-run byte-compare at the algorithm level (operator
  determinism already pinned bit_exact in sidecars).
- Scale: bounded logical-scale invariants + env-opt-in large test where needed;
  memory bounded by documented frame counts (PERFORMANCE.md).
- All local; `QT_QPA_PLATFORM=offscreen`, `ctest -j1`, targeted `-R` filters.
