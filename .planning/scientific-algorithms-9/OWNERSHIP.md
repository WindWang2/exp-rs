# OWNERSHIP — Scientific Algorithms 9.0

## Owned by this track

- `src/processing/algorithms/**` — scientific kernel semantics
  (terrain, hydrology, SAR, spectral, temporal, classification support,
  primitives) and their headers.
- `src/operators/rs/**` — algorithm-semantics parts of the RS operators
  (kernel invocation, unit/scale/NoData contracts, output metadata honesty).
- `src/processing/contracts/**` — the numeric-domain / scale contract layer.
- Algorithm metadata sidecars + help content bound to algorithms
  (`data/help/*` only where an algorithm change makes text wrong — minimal,
  mechanical edits).
- Scientific known-answer/regression tests:
  `tests/test_terrain_*`, `tests/test_sar_*`, `tests/test_spectral_*`,
  `tests/test_scientific_contracts.cpp`, `tests/test_e2e_open_issues.cpp`
  and new test files added by this track.
- `docs/processing/*.md` — additive/consistency edits to the scientific
  domain docs.
- `.planning/scientific-algorithms-9/**`.

## Shared seams (minimal, end-of-milestone, additive)

- `tests/CMakeLists.txt` — additive `sicnu_add_test(...)` lines only.
- `src/processing/CMakeLists.txt`, `src/operators/CMakeLists.txt` — additive
  source lines.
- `CHANGELOG.md` — one entry at track end.
- `src/operators/rs/rs_operators_init.cpp` — additive registration lines only
  (no schema/signature rewrites).
- `data/help/commands.json` / `diagnostics.json` — only when an owned operator
  contract changes; single-pass, end of track.

## NOT owned (do not modify)

- `src/workflow/**`, `src/processing/framework/**` (scheduler chain:
  WorkflowRunCoordinator → TaskCenter → JobEngine).
- `src/agent/**` (Pi runtime, harness, cartography, mapspec).
- `src/app/**`, `src/gui/**`, `src/ui/**` (Qt workbench).
- `src/geospatial/**` (generic geospatial I/O authority — consumed via the
  established `GdalDatasetWrapper` / reader seams in `src/processing/gdal`).
- `src/data/**`, `src/dataset/**`, `src/experiment/**` (stores).
- `src/plugins/**`, `src/sdk/**`, `src/runtime/**`, `src/model/**` areas.
- Scheduler, agent loop, QGIS renderer, model runtime: never re-implemented.

## Parallel-track coordination

- `feat/execution-concurrency-lifecycle-9` (worktree `exp-rs-exec-concurrency-9`)
  owns concurrency/scheduler surfaces; disjoint from this track's files.
- A defect-sweep remediation sits uncommitted in the shared `main` checkout
  (targets #848–#882 broadly, including files this track owns:
  `terrain_flow.cpp`, `sar_terrain.cpp`, `rs_spectral_index_operator.cpp`,
  `scientific_contracts.cpp`). This track does not read or depend on those
  uncommitted edits; if that sweep lands on master first, rebase conflicts in
  owned files resolve in favor of whichever fix is scientifically more
  complete — this track's regression tests are the arbiter.
