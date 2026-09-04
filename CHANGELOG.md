# Changelog

All notable changes to the `exp-rs` project will be documented in this file.

## [Unreleased] - 2026-09-04

### 🛰️ Pi Spatial Scientist & Cartography Workbench 3.0 (ADR 0127/0128)
- **Spatial Reasoning Contracts (`src/agent/contracts/`)**: versioned structured documents — DatasetUnderstanding, CapabilityCandidate, PreflightResult, ExecutionPlan, ResultAssessment, MapQualityReport — with builders, validators, and bounded-output helpers (`paginate`, serialized-size caps).
- **Workspace Understanding 3.0 (`workspace_state.*`)**: `WorkspaceState` document with stable entity ids (`asset-N`, `layer-N`, `collection-N`, `layout-N`) persisted in project properties; visible/selected/active layers, selected-feature counts, layouts, models, running tasks, recent outputs, and workflow-run summaries via a provider seam (no agent→workflow link dependency). Exposed through `spatial:workspace_summary` and `spatial:layer_summary`.
- **Bounded Inspection Tools**: `spatial:sample_pixels` (≤64 point reads, CRS transform), `spatial:sample_features` (≤20 attribute rows, attribute filter), `spatial:compare_rasters` (grid compatibility + decimated difference verdict: identical/within_tolerance/different/incomparable).
- **Capability Discovery (`spatial:search_capabilities`)**: fused ranking over the unified catalog — task-text relevance, band-role affinity, GPU fit, large-raster safety, determinism — returning CapabilityCandidate summaries with reasons/warnings/estimated cost; schemas stay behind `get_tool_schema`.
- **Automatic Model Selection (`spatial:select_model`)**: task-contract driven ranking over `ModelCatalog::rankModels` with readiness and artifact resolution surfaced; no hardcoded model names.
- **Static Workflow Preflight (`workflow:preflight`)**: schema/topology/operator/param/input/output/model checks with `WF_*` codes, repairability flags, and suggested actions — planners fix DAGs before executing.
- **Result Assessment (`spatial:assess_result`)**: empty-output, nodata-ratio, constant-output, value-range/class-count expectations, CRS presence, vector geometry validity → ResultAssessment verdict (pass/warn/fail) with provenance echo.
- **MapSpec (`src/agent/mapspec/`, ADR 0127)**: declarative cartographic document (page, map_frames, layers, legends, north arrows, scale bars, titles, labels, charts, colorbars, grids, annotations, source notes, constraints) with strict validation, v0→v1 migration, id-stable patch ops, `MapSpecCompiler` (MapSpec → QgsPrintLayout via LayoutService item factories) and best-effort `extract` roundtrip.
- **Component & Template Libraries (`data/cartography/`, `src/agent/cartography/registry.*`)**: 10 component descriptors (variants, layout constraints, compatibility) and 8 map templates (semantic slots → concrete MapSpec drafts via `cartography:instantiate_template`), with embedded fallbacks for headless runs.
- **Charts as First-Class Components**: `ChartRegistry` workspace entities (`chart-N`), QGIS-native `QgsLayoutItemChart` binding for layer-expression series (bar/line), QPainter inline renderer (bar/line/pie/area/scatter/histogram — no QtCharts), and color-bar rendering.
- **Compose → Preflight → Repair Loop (`cartography:compose/preflight/repair`)**: MapQualityReport with `code/severity/item_id/repairable/suggested_action` issues and 0–100 quality score; deterministic repair passes add missing furniture, move off-page items, bump tiny fonts, separate overlaps.
- **Symbology Intelligence (`symbology:describe/apply_categorical/apply_graduated/apply_raster_ramp`)**: bounded scans, bounded category counts, previous-renderer capture with rollback.
- **Workspace Command Model (`src/agent/commands/`, `workspace:undo/redo/history`)**: unified transaction stack for agent/symbology workspace mutations; layout mutations keep QGIS-native `QgsLayoutUndoStack`.
- **MCP & Pi Bridge**: `cartography:`/`symbology:`/`workflow:`/`workspace:` namespaces routed to the SpatialToolRegistry; Pi extension default `EXP_RS_TOOL_CATEGORIES` extended accordingly.
- **Benchmark**: `tests/test_spatial_scientist_benchmark.cpp` — 111 structured tasks across data understanding, tool discovery, interaction, scientific analysis, model selection, workflow, cartography, repair, and bounded-context graders, graded against live registries.
- **Tests**: new suites for contracts, workspace state/entity ids, MapSpec model/compiler/roundtrip, registries, charts, preflight/repair, symbology rollback, workflow preflight, capability ranking, and the benchmark harness.

## [Unreleased] - 2026-08-24

### 🚀 Pi-Based Spatial Intelligence Layer (ADR 0122)
- **Pi Adapter (`pi/`)**: TypeScript Pi extension (`exp-rs-spatial.ts`) bridging the exp-rs MCP server into the Pi agent runtime — spawn + handshake + `tools/list` → `pi.registerTool`, abort-aware dispatch, output truncation, and a `wait_for_execution` convenience tool. Includes the agent knowledge base (`pi/knowledge/`).
- **Spatial Tool Framework**: `SpatialTool` contract (`name/description/input_schema/execute/output_schema`) with a process-wide registry; `SpatialToolProvider` feeds the unified `AgentToolCatalog`; `spatial:` joins the MCP allow-list and executes inline.
- **Spatial Inspection Tools**: `spatial:raster_inspect` (GDAL metadata, band roles, wavelengths, nodata, radiometric state, optional subsampled statistics) and `spatial:vector_inspect` (OGR layers, schemas, extents, sampled features).
- **Agent Workflows**: MCP meta tools `run_workflow` (agent-generated pipeline JSON → `TaskCenter::submitPipelineJson`, per-step execution ids) and `get_workflow_status` (aggregate DAG status).
- **Model Runtime Catalog**: `ModelCatalog` scanning `models/*/model.json`; exposed via `spatial:list_models`; `rs:infer` resolves catalog names to weight paths.
- **Algorithm Capability Sidecars**: `AlgorithmMetaStore` over `data/processing/algorithm_meta/*.json` (task/input/output/gpu/accuracy) merged into `list_algorithms` / `search_algorithms` / `get_algorithm_schema` responses.
- **MCP `tools/list`**: now enumerates the unified tool catalog (algorithms, interaction, data, spatial) with full JSON Schemas alongside the meta tools.
- **Documentation Sync**: README (Spatial Intelligence section, corrected test count to 1,758 Catch2 cases, current architecture tree), CLAUDE.md (architecture map + language note), CONTEXT.md (new domain terms: Spatial Tool / Spatial Tool Registry / Model Catalog / Algorithm Capability Sidecar / Pi Bridge; ADR 0062–0122 index), docs/repo-layout.md, HANDOFF.md.

## [Unreleased] - 2026-08-03

### 🚀 Features & Deepening Architecture
- **Pipeline Status Enrichment**: Integrated `PipelineStatusResolver` callback injection in `WorkflowSession`, enabling authoritative DAG pipeline step completion status sync from `TaskCenter` without circular library dependencies.
- **Dynamic Worker Pool Control**: Implemented `PythonWorkerProcessPool::setPoolSize(int)` with dynamic grow/shrink capabilities and busy-worker protection.
- **Viewport Encapsulation**: Refactored `ActiveViewHost::viewportSnapshot()` to consolidate map canvas state into value-semantic `ViewportSnapshot` structs with single-point null safety.

### 🛡️ Security & Quality Fixes
- **IPC Socket Permissions (SEC-001)**: Restricted `QLocalServer` Unix domain socket permissions to `QLocalServer::UserAccessOption` (User-only `0700` access) to prevent multi-user local privilege escalation.
- **Transactional Upfront Shrink**: Added idle-count pre-validation to `PythonWorkerProcessPool::setPoolSize()` to guarantee atomicity and prevent pool size state desynchronization.
- **Qt6 Deprecation & Macro Safety**: Replaced deprecated `qMax` with `(std::max)` for header safety on Windows (`NOMINMAX`).
- **Container Growth Cap**: Enforced definition step bounds on `WorkflowSession::markStepComplete` and `snapshot()` step IDs.

### 🛠️ Agent & Tooling Integration
- **Agent Guidelines (`AGENTS.md`)**: Configured project-scoped behavioral rules integrating Andrej Karpathy's 4 core guidelines (Think Before Coding, Simplicity First, Surgical Changes, Goal-Driven Verification).
- **Skill Suites**: Installed `karpathy-guidelines` and the full `gstack` 59-skill suite for automated PR reviews, security auditing, and performance benchmarking.

---
*Verified against full Catch2 test suite (1,138/1,138 assertions passing).*
