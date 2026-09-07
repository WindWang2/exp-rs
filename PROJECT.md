# SICNU GEO RS (exp-rs) — Project State

Living project-state document. Claims here must be provable from the code and
tests at HEAD; do not hardcode transient counts (PRs, worktrees, test tallies)
— record those as dated evidence in planning dossiers instead.

## What this is

A pure C++20 remote-sensing analysis workbench built on the QGIS engine
(Qt 6.8+, GDAL/PROJ/GEOS, OpenCV 5, Catch2 v3). One desktop application
(`src/app`, `sicnu_geo_rs`), one headless CLI (`src/cli`), an MCP server and
Pi agent adapter over the same Task Center seam.

## Architecture map

- `src/core`, `src/gui` — QGIS engine (layers, rendering, CRS, canvas).
- `src/processing` — algorithm engine, providers, Task Center, Tool Call
  Dispatcher, kernels in `algorithms/`.
- `src/operators` — RSOperator framework + `rs:`/`gdal:`/`otb:`/`opencv:`
  families (JSON parameter/result seam, registry, determinism grades).
- `src/jobs` — JobEngine (execution workers, listeners, retention).
- `src/data` — DataManager (asset authority) + governance store/services.
- `src/analysis` — classification pipeline, segmentation, georeferencing.
- `src/workflow` — workflow runtime, session, pipeline editor canvas.
- `src/app` — desktop shell: ribbon workbench, panels (data / governance /
  layers), dialogs, task center UI, schema form builder, design tokens
  (`design_tokens.h`). See `docs/ui-architecture.md` for the information
  architecture and extension rules.
- `src/agent` — copilot, MCP, spatial tools, agent contracts; `pi/` bridge;
  MapSpec cartography (`src/agent/mapspec`, `src/agent/cartography`, `data/cartography`).
- `docs/adr/` — decision ledger (0001–0133); `CONTEXT.md` — domain vocabulary.

## Current state (2026-09-06)

- **Data Plane, Runtime, Governance & Reproducibility Reliability 4.0** (ADR 0130):
  document-authority/downgrade guard, WAL-consistent snapshots, checked store writes,
  reference-safe CAS eviction, external-mutation cache invalidation, crash-resume
  completion identity, truthful run states, bounded warm worker pool (`docs/architecture/FAULT_MATRIX_4.md`).
- **Cartography Design System 4.0** (ADR 0130/0131): design tokens,
  component/template library expansion, MapSpec 2.0 compositional
  constraints, composition solver, preflight/repair rule catalog, and the
  deterministic visual-regression harness (`docs/cartography/`).
- **Desktop Workbench & Unified UX 4.0** (ADR 0130–0133): unified shell
  (wired governance dock, project-context title, dead-panel removal, menu dedup),
  schema-validated operator forms, thin-client operator promotions (band tools,
  enhancement, pan-sharpen), grouped pipeline tasks + shared result renderer,
  C++ design-token layer, keyboard guardrail.
- **Scientific Algorithms & Processing Foundation 4.0** (ADR 0130):
  shared NoData/statistics/grid/histogram kernels, #759 fix, `rs:temporal_sen_trend`,
  validation policies (`docs/processing/`).
- **Project Workspace, Data Governance & Reproducibility Platform 3.0**
  (ADR 0129): governance store, workspace services, project format v3,
  crash-safe saves, workspace UI.
- **Multimodal SpatioTemporal RS Platform 3.0**: SAR operator family, temporal
  fit kernels, feature cube, model runtime 3.0, tile inference 2.0.
- **Pi Spatial Scientist & Cartography Workbench 3.0** (ADR 0127/0128): spatial
  reasoning contracts, MapSpec cartography, symbology intelligence, benchmarks.
- **Pi-Based Spatial Intelligence Layer** (ADR 0122): spatial tools, MCP
  catalog, model catalog, algorithm sidecars.

## Contracts that outlive any single PR

- Execution seam: `UI → TaskCenter → JobEngine → RSOperator → kernel`. GUI
  code must not run raster kernels inline (`test_ui_task_center_contract`).
- Data/Display seam: `DataManager` owns assets; canvas presentation goes
  through `ActiveViewHost`/`QgisDisplayManager`.
- Schema form contract: operator schemas are the single source of truth for
  defaults/ranges/required in any generated parameter UI.
- Design tokens: `SicnuUi::Tokens` mirrors the QSS token headers; the parity
  test fails on drift.
- Scale: workspace browsing stays model/view and paged (100k assets,
  fetchMore, 200/page); no per-row widget explosion.

## Build & test contract

- Configure: `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON` in
  a build dir (presets in `CMakePresets.json`: `dev-default`, `ci-fast`, `ci-full`,
  `sanitizer-debug`, `release-package`).
- Build: `cmake --build build` (bounded parallelism on shared hosts).
- Tests: `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest --test-dir build --output-on-failure`
  (CTestCustom pins Python/Qt env; see `TEST_INFRA.md`). Treat "fully green" claims as valid only with a fresh ctest log.
- Scientific validation policy and tolerance grades for algorithm kernels:
  `docs/processing/validation-policy.md`.

## Known limitations / open threads

- `module:classify:*` and `module:georef:*` flows are TaskCenter-tracked but
  not `rs:` operators (interactive sessions; documented in
  `docs/ui-architecture.md` §4).
- Pipeline editor keeps its slate-canvas badge palette (documented design
  exception to the token layer).
- The historical "2126 tests / 100% green" style figures are stale evidence;
  never claim suite health without a fresh `ctest` log.
