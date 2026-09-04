# Contract Audit: Temporal Workspace ↔ Agent/MCP ↔ Tool Catalog

## 1. Scope & Objectives
This audit assesses the alignment, invariants, and regressions across three core platform layers:
1. **Tool Schema & Exporter Contracts**: `SpatialTool` input/output schemas, `AgentTool` normalization, OpenAI function definitions, and MCP `tools/list` schema formats.
2. **Temporal Scene Semantics**: `temporal:register_collection` inline scene parsing, operator inline scene input parsing, and descriptor roundtrips.
3. **Algorithm Metadata Drift Gate**: Generated sidecars in `data/processing/algorithm_meta/`, CLI `--export-catalog`, and automated drift verification.

---

## 2. Seam Tracing & Current State

### Layer 1: Tool Schema & Exporter Seam
- **Contract Definition**:
  - `SpatialTool::inputSchema()` in `src/agent/spatial_tools/spatial_tool.h:98`:
    `/// JSON Schema ({type: "object", properties, required}) for execute() input`
  - In `src/agent/spatial_tools/temporal_workspace_tools.cpp`:
    - `TemporalListCollectionsTool::inputSchema()` (lines 129–143): missing `"type": "object"`.
    - `TemporalGetCollectionTool::inputSchema()` (lines 206–219): missing `"type": "object"`.
    - `TemporalRegisterCollectionTool::inputSchema()` (lines 284–301): missing `"type": "object"`.
    - `TemporalRemoveCollectionTool::inputSchema()` (lines 418–430): missing `"type": "object"`.
    - `TemporalIngestStacTool::inputSchema()` (lines 481–521): missing `"type": "object"`.
- **Adaptation & Export**:
  - `SpatialToolProvider::provideTools()` (`src/agent/spatial_tools/spatial_tool_provider.cpp:35`) passes `spatial->inputSchema()` into `AgentTool::inputSchema`.
  - `AgentTool::toOpenAiToolDefinition()` (`src/agent/tool_catalog/agent_tool.cpp:84-88`) and `AgentTool::toMcpToolDefinition()` (`lines 107-111`) directly set `funcObj["parameters"] = inputSchema;` and `root["schema"] = inputSchema;`.
  - **Regression Trigger**: In `tests/test_agent_tool_catalog.cpp:255,281`, assertions checking `fn["parameters"]["type"].asString() == "object"` and `item["schema"]["type"].asString() == "object"` fail because `fn["parameters"]["type"]` is `null`.

### Layer 2: Temporal Scene Semantics Seam
- **Schema Promise**:
  - `TemporalRegisterCollectionTool::inputSchema()` advertises:
    `"Scene entries: path strings or {path, time?, bands?, mask_band?, quality_band?}"`.
- **Execution Reality**:
  - `TemporalRegisterCollectionTool::execute()` (`temporal_workspace_tools.cpp:330-360`) only reads `path` and `time`, then calls `TemporalCollection::fromScenePaths(paths, times, ...)`.
  - `bands`, `mask_band`, `quality_band`, `asset_id`, `asset_revision`, and all multimodal attributes are silently discarded.
- **Operator Divergence**:
  - `rs_temporal_collection_input.cpp:42-100` (`fromInlineScenes`) independently parses `bands`, `quality_band`, `mask_band`, `asset_id`, `asset_revision`.
  - However, it calls `fileExists(path)`, which rejects remote COG URLs (`/vsicurl/...` or `https://...`), and it omits multimodal attributes.
- **Roundtrip Loss**:
  - `TemporalCollection` serializes via `TemporalSceneRef::toJson()`, which does serialize `bands`, `mask_band`, etc. But because `temporal:register_collection` discarded them before saving to `DataManager`, reloading from the workspace descriptor permanently loses these properties.

### Layer 3: Algorithm Metadata & Drift Gate Seam
- **Single Source of Truth**:
  - In-code operator `metadata()` provides `meta["task"] = "..."`, which populates `AlgorithmDescriptor::agentMetadata.taskFamily`.
- **Shipped Inventory**:
  - Exactly 6 operators declare `meta["task"]`: `gdal:polygonize`, `rs:change_detection`, `rs:infer`, `rs:qa_mask`, `rs:spectral_index`, `rs:supervised_classification`.
  - Exactly 6 sidecars are checked into `data/processing/algorithm_meta/`.
- **Current Gate Deficiency**:
  - `tests/test_algorithm_meta_drift.cpp:28` only tests `REQUIRE(store.loadDefaults() >= 6);`.
  - At line 73, unresolvable IDs produce `WARN(...)` and `continue`, failing to fail the test.
  - No verification exists that every task-declaring operator has a matching sidecar, that no orphaned sidecars exist, or that serialized JSON matches disk bytes.
- **CLI Export**:
  - `src/cli/main_cli.cpp:185-255` implements `--export-catalog` inline in `main()`, making it untestable by Catch2 without subprocesses.

---

## 3. Deferred Items (Out of Scope for this Worktree)
1. **Issue #728**: Remote COG / STAC canonicalization, signed query URLs, and date-range inclusivity (recorded in `DEFERRED_728.md`).
2. **Issue #730 Environmental Issues**: Embedded Python plugin manager, libxml2, miniconda, and fcitx5 test harnesses (recorded in PR mapping; this worktree only fixes the product-side `AgentToolCatalog` regression).
