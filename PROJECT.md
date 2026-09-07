# Project: SICNU GEO RS — Spatial Scientist Platform

## Architecture
- Repository: `exp-rs` (C++20 / Qt 6.8+ / GDAL / PROJ / GEOS / OpenCV 5 / Catch2 v3.7.1)
- Main Branch: `master`
- Build System: CMake (Ninja; presets `dev-default` / `ci-fast` / `ci-full` / `sanitizer-debug` / `release-package` in `CMakePresets.json`)
- Test Runner: CTest with Catch2. `CTestCustom.cmake` pins `PYTHONHOME`/`PYTHONPATH` and `QT_IM_MODULE=compose` (see TEST_INFRA.md). Do not treat "100% green" as a current claim without a fresh `ctest` log.
- Agent surface: Pi (generic agent loop, TS bridge in `pi/`) drives ExpRS as the
  geospatial/remote-sensing harness (ADR 0122/0130): MCP server + unified
  tool catalog + `harness:*` plan lifecycle + `WorkflowRunCoordinator` →
  `TaskCenter` execution.

## Capability Inventory (long-term)

| Area | What exists | Where |
|---|---|---|
| Operators | ~95 headless JSON-seam operators (spectral indices, change detection incl. SAR, OBIA, temporal/phenology, fusion, classification, inference) with determinism grades + resource estimates | `src/operators/` |
| Workflow engine | 10-state run FSM, atomic checkpoints, cross-process locks, partial-failure resume, deterministic execution cache, artifact GC | `src/workflow/` |
| Task execution | TaskCenter: profiles, RSS watermark + RAM-budget admission, cancel cascades, manual retry | `src/processing/framework/` |
| Agent harness | Stable error taxonomy, tool taxonomy/manifests (risk class, resource hints, preconditions, expected artifacts), entity resolution, typed context, deterministic scientific preflight (ndvi/change/sar_change/classify/phenology), AgentPlan v2 → WorkflowDefinition compiler, run verification (PASS/PASS_WITH_WARNINGS/FAIL), final map confirmation, bounded transient resume, scientific recipes | `src/agent/harness/`, `data/agent/recipes/` (ADR 0130) |
| Spatial tools | Bounded inspection/sampling/assessment/capability tools; temporal collections; MapSpec cartography with compose/preflight/repair; symbology; workspace commands; governance surfaces | `src/agent/` |
| Model runtime | ModelCatalog (26 manifests), tile inference engine, ONNX/OpenCV-DNN providers, `rs:infer` preflight | `src/operators/` |
| Governance | Project workspace identities, SQLite governance store, lineage, snapshots, relink, audit | `src/data/governance/` (ADR 0129) |
| Plugin SDK | C++ plugin contracts, permission model, external-tool operator | `src/sdk/`, `src/plugins/` |

## Interface Contracts
- Execution: agent plans compile to `WorkflowDefinition` JSON and run only
  through `WorkflowRunCoordinator` → `TaskCenter` (single engine, ADR 0123).
- Agent tools: one unified `AgentToolCatalog`; every entry carries a bounded
  `harness` manifest block; MCP `tools/list` is compact by default.
- Errors: harness failures surface the stable taxonomy
  (`src/agent/harness/harness_error.h`); verification verdicts are
  PASS / PASS_WITH_WARNINGS / FAIL and FAIL forces run status `failed`.

## Code Layout
- Core libraries: `src/core`, `src/gui`, `src/data`, `src/processing`, `src/operators`, `src/workflow`, `src/jobs`, `src/agent` (incl. `harness/`)
- Applications: `src/app` (`sicnu_geo_rs`), `src/cli` (`sicnu_geo_rs_cli`)
- Pi bridge: `pi/` (TS extension, knowledge base, subagent role cards)
- Tests: `tests/` (Catch2 test executables); agent metadata: `.agents/`
