# OWNERSHIP — flash-spectral-intelligence-13

## Allowed to change (owner)

Kernels / operators / library (core change surface):

- `src/processing/algorithms/spectral_cem.{h,cpp}` (shared accumulation reuse; minimal edits)
- `src/processing/algorithms/spectral_tcimf.{h,cpp}` (new)
- `src/processing/algorithms/spectral_osp.{h,cpp}` (new)
- `src/processing/algorithms/spectral_detection.{h,cpp}` (background-raster plumbing, minimal)
- `src/processing/algorithms/spectral_spatial_fusion.{h,cpp}` (bilateral method)
- `src/processing/algorithms/spectral_library_scale.{h,cpp}` (consolidation follow-ups only)
- `src/processing/algorithms/spectral_library.{h,cpp}` (only if consolidation requires)
- `src/processing/algorithms/spectral_resampling.{h,cpp}` (only if grid reconciliation needs)
- `src/operators/rs/rs_spectral_detection_operators.{h,cpp}` (new kinds + background raster)
- `src/operators/rs/rs_spectral_spatial_fuse_operator.{h,cpp}` (method param, streaming)
- `src/operators/rs/rs_spectral_reference_input.{h,cpp}` (background raster grid seam, minimal)
- `src/operators/rs/rs_library_select_operator.cpp` (rewire inline SAM to shared kernel)
- `src/operators/rs/rs_operators_init.cpp` (register new operators)
- `src/agent/spatial_tools/spectral_spatial_tools.cpp` (library path resolution, minimal)
- `src/contracts/scientific_contract.{h,cpp}` (new contract rows, append-only block)
- `src/core/CMakeLists.txt` (drop deleted `spectral_library.cpp` entry)
- `src/processing/CMakeLists.txt`, `src/operators/CMakeLists.txt` (append new sources)

Deletions (consolidation, evidence-backed):

- `src/core/spectral_library.{h,cpp}` — production-dead duplicate (no `src/` caller);
  test-only call sites rewired to the authority. No data file uses its schema.

Tests (owner):

- `tests/test_spectral_cem.cpp`, `tests/test_spectral_detection.cpp`,
  `tests/test_spectral_detection_streaming.cpp`, `tests/test_spectral_spatial_fusion.cpp`,
  `tests/test_spectral_library*.cpp`, `tests/test_spectral_selection.cpp`
- new: `tests/test_spectral_tcimf.cpp`, `tests/test_spectral_osp.cpp`,
  `tests/test_spectral_background_raster.cpp` (or extend streaming file),
  `tests/test_spectral_fusion_edge.cpp` (or extend fusion file)
- `tests/synthetic_raster_builder.h` — add wavelength-metadata support (shared test
  fixture; additive only, coordinate with other tracks via review)
- `tests/CMakeLists.txt` — **append-only at EOF** (repo convention; #1135 inserts at
  ~6067, #1136 at ~10036 — do not insert near them)

Generated surfaces (owner; regenerate only via repo tools):

- `data/processing/algorithm_meta/*.json` (`--export-catalog`)
- `data/processing/algorithm_meta/capability/*.json` (`gen-meta`)
- `data/agent/capabilities/spectral_transform.json` (authored rows; generator-consistent)
- `data/contracts/determinism_census.snap.json`, `data/contracts/contract_graph.snap.json`
  (`contract_inventory`)
- `pi/knowledge/capability-hyperspectral.md` (`gen-pages`)
- `tests/test_algorithm_meta_drift.cpp` pin bump (55 → N)
- `tests/test_contract_cross_surface_11.cpp` expectedMissing set (only if new nodes)

Docs (owner):

- `docs/adr/0167-spectral-intelligence-13.md` (new; 0166 is claimed by open PR #1136)
- `.planning/flash-spectral-intelligence-13/*`
- `.goal-loop-ledger.md` (append at EOF only)

## Forbidden (do not touch)

- SAR calibration / polsar (`src/operators/rs/rs_sar_*`, `src/processing/algorithms/sar*`)
- Temporal phenology (`src/processing/algorithms/temporal/*`, `rs_temporal_*`,
  PR #1135's in-flight work)
- Model runtime / ensembles, workflow engine, plugin SDK, geospatial fabric,
  dataset/experiment stores, labs/classroom, CLI commands outside spectral
- `data/spectral/library.json` + `data/spectral/library.schema.json` content (curated
  dataset; tests guard it — do not regenerate or edit)
- Any `src/gui`, `src/app` code except the library dialog write-path fix if approved
  in DECISIONS

## Shared conflict hotspots (with mitigation)

1. `tests/CMakeLists.txt` — append at EOF only.
2. `data/contracts/*.snap.json` + `contract_graph.snap.json` — regenerate wholesale from
   the union tree at PR time (after fetching latest master); never hand-merge.
3. `tests/test_algorithm_meta_drift.cpp` — bump pin; textual conflict with #1135 likely,
   resolve by taking the union of both bumps (both are count-only edits).
4. `.goal-loop-ledger.md` — append at EOF.
5. `docs/adr/` — take 0167 (0166 claimed by #1136).
6. `tests/synthetic_raster_builder.h` — additive methods only; if another track edits it,
   rebase and re-apply additively.
7. `data/agent/capabilities/*.json` (#1151) — the capability-knowledge mirror is
   hand-authored and NOT covered by any generator: after a union merge, run
   `test_capability_drift` (both coverage floors — operators AND tools) and author
   entries for anything the merge added or dropped before the final gate set.
