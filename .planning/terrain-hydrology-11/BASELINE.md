# BASELINE — terrain-hydrology-11 (Phase 0 audit, 2026-09-15)

## Raw facts at start (main repo, read-only audit)

- `git fetch origin --prune` → exit 0.
- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  (`a5b11b7f10 fix: fail-closed fixes for review issues #994–#999 (#1000)`).
  **Note:** prompt-generation snapshot said `ebcafb4d02`; master has since advanced —
  D18 (#991) and D19 (#992) are now MERGED (in the last-20 log). Fresh facts win.
- Recent master log (top 10):
  ```
  a5b11b7f10 fix: fail-closed fixes for review issues #994–#999 (#1000)
  1cea98921b Merge branch 'grok/dataset-foundry-benchmark-d19' (#992)
  c5d4aafe8e D18: Unified Mission Workbench — MissionContext + D14/D15/D17 mounts (#991)
  77e178ac1e fix(ci): resolve macOS/Windows compile errors in d17 and Win32 paths (#993)
  08264801a0 docs(d19): record M6 hermetic scale/evolution/LeaveOne evidence
  e57534384f test(d19): hermetic 100k catalog scale, version evolution, leakage Fail, LeaveOne*
  753d80e4a0 docs(d19): record persistence, agent tools, and honest not-executed cmake
  1ae474541f feat(agent): D19 foundry/benchmark tool wrappers and hermetic E2E
  343ab33d3f feat(experiment): persist BenchmarkService via ExperimentStore
  344f26bef3 feat(experiment): D19 benchmark definition, headless runner, and pins
  ```
- Open PRs (1):
  - **#1008** `zcode/radiometric-spectral-workbench` — "Day 13 radiometric calibration,
    6S atmospheric correction & spectral workbench". base master, NOT draft,
    mergeStateStatus **CONFLICTING** (with master). Changed files: spectral/radiometric
    sources, `src/agent/spatial_tools/spatial_tool.cpp`, several CMakeLists,
    `.gitignore`, tests, `.planning/radiometric-spectral-workbench/`.
    → No overlap with terrain business files; shared-file overlap listed in
    PARALLEL_OWNERSHIP.md.
- Open issues (7): #1001 (io:clip CRS), #1002 (workflow registry executor fail-open),
  #1003 (dataset join null columns), #1004 (dataset:qa scan_capped), #1005 (georef
  mapPick CRS throw), #1006 (workflow soft-default syntheticExecute), #1007
  (dataset:qa CRS audit). **None are terrain/hydrology.** None block this track;
  none duplicated by this track (out-of-scope domains: io/workflow/dataset/georef).
- `ISSUES.md` = old D3 lab-content-expansion backlog (temporal/SAR/hyperspectral/
  cartography operator gaps). Spot-checked against current code: it predates the
  10.0 platform tracks; **not treated as live backlog** for this track (none of its
  items are terrain).
- Host: 16 cores, 62 GiB RAM (≈54 GiB available at audit). Linux, zsh.
  Concurrent tracks may run on the same host → build `-j2` cap enforced.

## Existing terrain capability on master (verified by reading code)

- `src/processing/algorithms/terrain_analysis.{h,cpp}` — slope/aspect (Horn 1981,
  anisotropic cellsize, degree-CRS metre conversion at #612), hillshade (iso/aniso/
  multidirectional), roughness, TRI (Riley), TPI (3×3), curvatures
  (Zevenbergen-Thorne, convexity-positive, Esri-style normalization), local relief.
- `src/processing/algorithms/terrain_flow.{h,cpp}` — `fillDepressions`
  (priority-flood, Barnes 2014, NoData-barrier seeding incl. interior NoData
  borders #848), `flowDirections` (D8 ESRI codes; flats = sinks = 0),
  `flowAccumulation` (topological peel, self-inclusive, NoData-excluded #783),
  `watershedLabels` (pour-point reverse BFS, first-label tie-break).
- Operators: `rs:terrain_analysis` (11 products, tiled 2048² 1-px-halo streaming via
  GdalBlockStream), `rs:terrain_flow` (4 products: fill/flow_direction/
  flow_accumulation/watershed; full-frame, dynamic memory estimate, supportsCancellation).
- UI: `src/app/dialogs/terrain_dialog.{h,cpp}` — 6 analysis products (slope/aspect/
  hillshade/roughness/tri/tpi); no hydrology/viewshed entries.
- Agent: NO terrain spatial tool (`src/agent/spatial_tools/` has raster_inspect,
  io, sample, temporal, spectral(#1008)… — nothing terrain-specific).
- Tests: `tests/test_terrain.cpp`, `tests/test_terrain_foundation5.cpp` (Catch2;
  fill/D8/accum/watershed/curvature/MD-hillshade known-answers + 2 operator E2E).
- Capability sidecars: `data/processing/algorithm_meta/capability/rs-terrain-flow.json`,
  `rs-terrain-analysis.json` (v2; product enum pinned → product additions require
  regeneration via `capability_knowledge_tool gen-meta`, enforced by
  tests/test_capability_knowledge.cpp "D8 drift").
- docs: `docs/processing/foundation-5.md` documents terrain family; explicitly lists
  debt: "Filled-flat routing in rs:terrain_flow is a sink (direction 0); no
  flat-resolution routing is attempted". No `docs/processing/terrain*.md` exists.
- README line 19 claims: "Terrain Analysis: Slope, aspect, hillshade, roughness,
  TRI, TPI" — README understates master (missing curvature/flow) and has no
  hydrology/viewshed.

## Verified gaps → this track's packages

| Package | Master state | Gap this track closes |
|---|---|---|
| A conditioning | fill yes; **flat resolution documented-debt missing**; no conditioning warnings | epsilon-gradient flat resolution; unit/vertical-datum warnings in operator result |
| B flow | D8 only; full-frame; manual pour points | D∞ (Tarboton 1997); automatic outlet/pour-point detection |
| C streams | watershed labels only; no stream network | threshold extraction, Strahler order, connectivity invariants, stream vectorization |
| D viewshed | **absent** | R2/R3 line-of-sight viewshed, observer/target heights, radius, curvature+refraction, multi-observer cumulative |
| E solar | hillshade only | horizon angles, shadow mask, shadow duration over sun track, hillshade series |
| F landform | TPI 3×3, curvatures | multiscale TPI + classification, geomorphons |
| G surface | dialog 6 products, no agent tool | dialog hydrology/visibility entries; `spatial:terrain_*` agent tools |
| H truth | ad-hoc per-test DEMs | `tests/synthetic_terrain_dem.h` shared closed-form factory (plane/cone/pit/ridge/channel) |

## Build environment facts

- Configure (repo pattern, from prior 10.0 tracks' EVIDENCE):
  `cmake --preset dev-default -G Ninja -DSICNU_LAB_SKIP_PYTHON_BINDINGS=ON` with
  TMPDIR redirected off the shared /tmp tmpfs.
- Hard caps: build `-j2` (→`-j1` under pressure), tests `-j1`,
  `QT_QPA_PLATFORM=offscreen`.
