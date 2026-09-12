# OWNERSHIP — Cartography Platform 9.0

## Owned by this track (may add/modify freely)

- `src/agent/mapspec/**` — MapSpec model, validation, conditions, compiler,
  extractor (M0/M2 surfaces).
- `src/agent/cartography/**` — composition solver, quality/preflight/repair,
  component+template+style+solution+chart registries, design tokens,
  typography, cartography agent tools.
- `data/cartography/{components,templates,styles,tokens}/**` and
  `data/cartography/index.json` — the declarative catalogs.
- `docs/cartography/**` — cartography docs (reference, migrations, rules,
  visual-regression methodology, limitations).
- `tests/test_mapspec.cpp`, `tests/test_cartography_*.cpp`,
  `tests/test_platform{5,6,7,8}*.cpp`, `tests/test_knowledge_drift.cpp`,
  `tests/benchmark_quality7.cpp` — the cartography test executables and any
  new `tests/test_platform9.cpp`.
- Cartography-specific LayoutService types: only the narrow seam in
  `src/agent/layout_tools/layout_service.{h,cpp}` this track already owns
  (item types added by cartography: line/polyline additions from 8.0).
  Shared-file edits stay minimal-increment and milestone-final.

## Shared files (minimal incremental edits, milestone-end only)

- `tests/CMakeLists.txt` — one target block for any new test executable.
- `CHANGELOG.md` — one entry per milestone series.
- `src/agent/CMakeLists.txt` — only when a new source file is added under
  `src/agent/cartography|mapspec` (they are already globbed? — verified:
  sources are listed explicitly; adding files requires touching this).
- `data/help/commands.json`, `data/help/diagnostics.json` — only when a new
  agent tool/diagnostic is added (help-coverage test enforces sync).

## Explicitly NOT owned (never modify)

- `src/app/**` (Workbench shell), `src/geospatial/**` (I/O authority),
  `src/workflow/**` (WorkflowRunCoordinator/TaskCenter), `src/jobs/**`,
  `src/operators/**`, `src/processing/**`, `src/dataset/**`,
  `src/experiment/**`, `src/plugins/**`, `src/sdk/**`, `src/runtime/**`,
  `src/agent/harness/**` (Harness track — recipe degradation #867 lives
  there), `src/agent/layout_tools/**` beyond the declared seam.
- Pi agent loop and execution scheduler chains — only called through the
  existing typed tool interfaces.

## Parallel-track conflict policy (10 concurrent worktrees)

The four open `-9` PR branches (#883–#886) and their worktrees
(`exp-rs-scientific-*`, `exp-rs-model-runtime-*`, `exp-rs-spatial-scientist-*`)
were checked: none modifies owned files (OVERLAP_MAP.md). This track avoids
`src/agent/harness/**` (spatial-scientist-harness-9's territory) except
through read-only reference; if a shared seam collision appears at PR time,
this track re-syncs onto latest master and integrates through the stable
typed-tool interfaces rather than editing the other track's files.
