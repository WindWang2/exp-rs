# Changelog

All notable changes to the `exp-rs` project will be documented in this file.

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
