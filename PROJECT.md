# Project: SICNU GEO RS (exp-rs) — Living Project State

> Living project-state document. Transient counts (open PRs, worktrees, test
> tallies) are recorded only as dated evidence, never as current-state claims.

## What this repository is

`exp-rs` / **SICNU GEO RS**: a pure C++20 remote-sensing analysis platform on
a vendored QGIS engine (Qt 6.8+, GDAL/PROJ/GEOS, OpenCV 5, Catch2 v3.7.1),
shipped as a desktop application (`sicnu_geo_rs`) and a headless CLI
(`sicnu_geo_rs_cli`), with an MCP server, an agent copilot, a plugin/SDK
ecosystem (`sicnu_sdk`, `exprs`), an out-of-process Python plugin host, and a
Pi agent-runtime bridge (`pi/`).

## Architecture map (stable seams)

- Execution: `TaskCenter` (single owner of algorithm task lifecycle) ←
  `ToolCallDispatcher` (agent tool calls) ← MCP server / Copilot / CLI.
  `JobEngine` is the worker pool under TaskCenter.
- Algorithms: `AtomicAlgorithmRegistry` (uniform adapter catalog) fed by
  providers (QGIS/GDAL/OTB/Generic-CLI/Python) and the Operator Registry
  (`rs:` operators, determinism-graded, ADR 0124).
- Data: `DataManager` asset authority (assets/collections/leases/revisions),
  workspace governance store (ADR 0129), project format v3.
- Display: `ActiveViewHost` facade over the map canvas; Display View/Layer
  separation (ADR 0015).
- Plugins: `exprs::PluginRegistry` lifecycle owner (ADR 0130: barrier-drained
  unload, UI reverse ownership, path containment, workspace effect policy);
  Python plugins out-of-process (ADR 0014); conformance via
  `sicnu_geo_rs_cli plugin test`.
- Agent: spatial tools (ADR 0122), spatial-scientist contracts (ADR 0128),
  MapSpec cartography (ADR 0127), Pi bridge.

Full directory map: `docs/repo-layout.md`. Domain vocabulary: `CONTEXT.md`.
Decision log: `docs/adr/0001`–`0131+`.

## Build & test (authoritative commands)

- Configure: `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
  -DSICNU_EMBED_PYTHON=ON -S . -B build`
- Build: `cmake --build build` (honor `CMAKE_BUILD_PARALLEL_LEVEL`).
- Test: `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`
  (CTestCustom.cmake pins PYTHONHOME/LD_LIBRARY_PATH/QT_IM_MODULE; see
  TEST_INFRA.md). Catch2 tests are discovered PRE_TEST by testcase name.
- Platform status: Linux validated locally; macOS via CI seam; Windows
  compiles the SDK targets (Win32 loader path), external-process execution is
  a typed refusal there — see docs/plugins/external-process.md.

## Current focus (as of 2026-09)

Goal-series epics running as separate worktrees/branches, each landing one PR:
plugin SDK lifecycle/isolation 4.0 (ADR 0130), scientific algorithms
foundation, workspace governance 3.0 hardening, desktop UX, cartography,
data runtime. Completed series: Extensibility/Plugin SDK 3.0 (#743), Project
Workspace & Governance 3.0 (#745), multimodal spatiotemporal platform,
Pi spatial-scientist layer.

## Known open defect clusters (issue-tracker truth at read time)

- Governance store hardening (#746, #750–#754, #758) — owned by the
  governance epic.
- Temporal RMSE NaN denominator (#759); stale PROJECT.md (#760 — this
  rewrite addresses it).
- Plugin/SDK lifecycle & security cluster (#747, #748, #755, #756, #757) —
  owned by the plugin SDK 4.0 epic (ADR 0130).
