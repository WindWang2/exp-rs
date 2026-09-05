# Changelog

All notable changes to the `exp-rs` project will be documented in this file.

## [Unreleased] - 2026-09-05

### 🗂️ Project Workspace, Data Governance & Reproducibility Platform 3.0 (goal series, ADR 0129)
- **Stable domain identities**: `sicnu::workspace` strong ids (Workspace/Dataset/Result/Experiment/SmartCollection/Export) with entity revisions and lifecycle vocabularies (legal result transitions draft→validated→approved/superseded/archived); paths are storage locators, never identity (`data/governance/governance_types.h`).
- **Governance store**: single SQLite WAL index (`<project>.governance.db`, schema v1, forward read-only tolerance) mirroring DataManager assets with enrichment (SHA-256 content fingerprint, sensor, modality, CRS, band roles, availability), plus datasets, results+inputs+artifacts, runs, experiments, cycle-safe recursive-CTE lineage edges, smart collections, exports, path mappings, paged faceted queries and an append-only audit log (`data/governance/governance_store.*`).
- **Project Format v3**: `<sicnuDataManager version="3">` keeps v1 blocks byte-compatible and adds one `<workspace>` JSON block; v1 files migrate in memory (full mirror, non-destructive read) and upgrade on next save; unknown sections reported then skipped; a downgrade guard re-persists the cached document when the store is unavailable (`app/data_project_serializer.*`).
- **Atomic crash-safe save**: `QgsProject::writeProjectFile` writes a temp file in the target directory, fsyncs and POSIX-replaces the target — a crash leaves the old or the new file, never a truncated one (`core/project/qgsproject.cpp`).
- **Workspace services**: `WorkspaceService` facade (mirroring, tagging, datasets, result lifecycle, runs, experiments, smart collections, impact analysis, producer lookup, audit) plus RelinkService (root moves + fingerprint-verified relink), WorkspaceValidator (machine-readable diagnostics with repair suggestions), MetadataPipeline (bounded incremental async verify/enrich with GDAL structure refresh), ImportCenter (bounded incremental scan + durable dedup), ReproBundleExporter (reference-only/metadata-only/portable), SnapshotService (collision-proof project+DB snapshots with pruning), CleanupService (protected/orphan plan, rows-only execution), WorkspaceTransactionStack (undo/redo) (`data/governance/*`).
- **Agent surfaces (Phase T)**: 11 bounded structured tools — `project:summary/search/health`, `asset:inspect/validate/relink`, `collection:query`, `lineage:upstream/downstream`, `result:inspect`, `run:compare` — every listing paged (≤100), MCP namespaces registered.
- **CLI (Phase U)**: `project validate|health|search|migrate|relink|lineage|export-manifest|audit` drive the same service layer headlessly (JSON envelope, stable exit codes).
- **Workspace UI (Phase S)**: 工作区治理 dock with paged `QAbstractTableModel` (fetchMore, 200/page), text/kind/state/sensor facets, governed details pane and bounded health check — no per-asset widgets.
- **Scale contract**: 100k assets (SICNU_WS3_STRESS=1): ingest 1.0s, paged query 84ms, facet 5–21ms, 1000 indexed point lookups 42ms, 10k bulk tag 14ms, depth-64 lineage <1ms (`benchmarks/workspace-governance-3-100k.json`).

## [Unreleased] - 2026-09-04

### 🛰️ Multimodal SpatioTemporal RS Platform 3.0 (goal series)
- **SpatioTemporal observation contracts**: typed `Modality` vocabulary + `ObservationContract` view over `TemporalCollection` (`spatiotemporal_contracts.h`, alias `SpatioTemporalCollection`); STAC ingest now maps SAR (`sar:polarizations`, `sar:instrument_mode`, `eo:gsd`) and DEM products; `inspectScene` populates modality/sensor/polarizations/radiometric state from product metadata (explicit inline claims always win); `ModalityProfile` preflight facts with new gates `temporal.modality_mismatch`, `temporal.polarization_mismatch`, `temporal.polarization_partial`, `temporal.dem_unit_undeclared`.
- **SAR operator family**: kernels in `processing/algorithms/sar/` (calibration σ0=(DN²−noise)/A², backscatter conversions, DEM plane-fit terrain flattening with layover/shadow mask, speckle incl. refined-Lee + multitemporal, GLCM texture, ratio/log-ratio) behind 8 unified operators: `rs:sar_calibrate`, `rs:sar_backscatter`, `rs:sar_terrain_flatten`, `rs:sar_terrain_correction`, `rs:sar_speckle`, `rs:sar_ratio`, `rs:sar_texture`, `rs:sar_change`. The speckle dialog is now a thin client over `rs:sar_speckle` (GUI executes no kernels).
- **Temporal Analysis 2.0**: pure fit kernels (`temporal_fit.h`: Savitzky–Golay, Whittaker banded solve, harmonic regression w/ IRLS, phenology metrics, greedy piecewise-linear breakpoints, seasonal decomposition) behind `rs:temporal_smooth`, `rs:temporal_gap_fill`, `rs:temporal_harmonic_fit`, `rs:temporal_phenology`, `rs:temporal_breakpoints`, `rs:temporal_decompose`.
- **Multimodal Feature Cube**: self-describing feature stacks (`processing/features/feature_cube.*`, dataset metadata + sidecar) behind `rs:feature_stack`, `rs:feature_normalize`, `rs:feature_select`; model-input matching for `rs:infer` preflight.
- **Model Runtime 3.0**: manifest v3 `inputs[]` (named multi-input), per-input `temporal_length`/`temporal_collapse`, `output.uncertainty` (entropy/margin); `IModelRuntime::inferMulti` with named-input OpenCV DNN support; optional ONNX Runtime provider behind `SICNU_WITH_ONNX_RUNTIME` (graceful stub without the dependency).
- **Tile Inference Engine 2.0**: multi-head output stacking with `SICNU_OUTPUT_HEADS` layout metadata, softmax-entropy/margin uncertainty bands, flip-TTA averaging, VRAM/RAM-budget-aware batch sizing.
- **Agent surfaces**: `WorkspaceSnapshot` collection info + `temporal:*` tools expose modalities/sensors/polarizations.
- **Model library**: 24 new high-quality manifest templates (buildings/roads/water/landcover/crops/forest/change/cloud/ship/airplane, UNet/SAM/YOLO/SegFormer/Swin/Siamese/temporal families, optical-SAR fusion, SAR water/flood/ship) with a catalog validation test (`test_model_library_manifests`).
- **Baseline repair**: regenerated `algorithm_meta` sidecars for the #738 taskFamily drift (7 sidecars, drift test updated from the pinned 6).
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
