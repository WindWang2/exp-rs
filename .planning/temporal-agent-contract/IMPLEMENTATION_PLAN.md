# Implementation Plan: Temporal Workspace ↔ Agent/MCP ↔ Tool Catalog Contract Convergence

## Phase 1: Tool Schema Contract & Invariant Normalization
1. **Fix Source Declarations**:
   - In `src/agent/spatial_tools/temporal_workspace_tools.cpp`, explicitly set `schema["type"] = "object"` in:
     - `TemporalListCollectionsTool::inputSchema()` & `outputSchema()`
     - `TemporalGetCollectionTool::inputSchema()` & `outputSchema()`
     - `TemporalRegisterCollectionTool::inputSchema()` & `outputSchema()`
     - `TemporalRemoveCollectionTool::inputSchema()` & `outputSchema()`
     - `TemporalIngestStacTool::inputSchema()` & `outputSchema()`
2. **Implement Canonical Seam Normalization & Fail-Fast Validation**:
   - In `src/agent/tool_catalog/agent_tool.h` & `agent_tool.cpp`:
     - Add `Json::Value normalizedInputSchema() const`.
     - Validates that non-empty `inputSchema` is an object.
     - Validates that if `"type"` is present, it must be `"object"`, throwing `std::invalid_argument` if an illegal type is specified.
     - Injects `"type": "object"` and ensures `"properties"` is an object.
     - Update `toOpenAiToolDefinition()` and `toMcpToolDefinition()` to use `normalizedInputSchema()`.

## Phase 2: Temporal Scene Semantic Parity & Unified Parsing
1. **Extract Unified Inline Scene Parser in `sicnu_processing`**:
   - In `src/processing/algorithms/temporal/temporal_collection.h` & `temporal_collection.cpp`:
     - Implement `TemporalSceneRef::parseInline(const Json::Value &v, int index, TemporalSceneRef *out, QString *error, bool inspectRaster = true)`.
     - Implement `TemporalCollection::fromInlineScenes(...)`.
     - Parse `path`, `time`, `timeSource`, `bands` (`bandOverrides`), `mask_band`, `quality_band`, `asset_id`, `asset_revision`, `modality`, `sensor`, `radiometric_state`, `resolution_m`, `cloud_cover_percent`.
     - Support remote COG URLs (`/vsicurl/...`, `http://...`, `https://...`) without failing `QFile::exists()`.
2. **Wire Unified Parser into Call Sites**:
   - In `src/agent/spatial_tools/temporal_workspace_tools.cpp` (`TemporalRegisterCollectionTool::execute`):
     - Replace ad-hoc path/time extraction with `TemporalCollection::fromInlineScenes(...)`.
   - In `src/operators/rs/rs_temporal_collection_input.cpp` (`fromInlineScenes`):
     - Replace ad-hoc parser with `TemporalCollection::fromInlineScenes(...)`.
   - In `src/agent/spatial_tools/temporal_collection_tools.cpp` (`collectionFromInput`):
     - Adopt `TemporalCollection::fromInlineScenes(...)`.

## Phase 3: Algorithm Metadata Exact Drift Gate & Testable Export
1. **Extract Testable Generator in `sicnu_processing`**:
   - In `src/processing/framework/algorithm_meta_store.h` & `algorithm_meta_store.cpp`:
     - Add `idToFileName(const std::string &id)`.
     - Add `primaryDataKind(const std::vector<PortDescriptor> &ports)`.
     - Add `entryFromDescriptor(const AlgorithmDescriptor &desc)`.
     - Add `serializeEntry(const AlgorithmMetaEntry &entry)`.
     - Add `generateCatalog(const std::vector<AlgorithmDescriptor> &descriptors)`.
     - Add `exportCatalog(const std::string &outDir, const std::vector<AlgorithmDescriptor> &descriptors, std::string *error = nullptr)`.
2. **Refactor CLI `--export-catalog`**:
   - In `src/cli/main_cli.cpp`, delegate directly to `AlgorithmMetaStore::exportCatalog(...)`.
3. **Upgrade Drift Test Gate**:
   - In `tests/test_algorithm_meta_drift.cpp`:
     - Assert exact two-way membership between task-declaring descriptors and disk sidecars.
     - Assert expected count == 6 (`gdal:polygonize`, `rs:change_detection`, `rs:infer`, `rs:qa_mask`, `rs:spectral_index`, `rs:supervised_classification`).
     - Assert byte-for-byte identity between `generateCatalog` output and disk files.
     - Fail fast on unresolvable IDs (replace `WARN` with `REQUIRE`).

## Phase 4: Verification & Test Suites
1. Run `test_agent_tool_catalog` (recovers green for "Schema Merge and Export").
2. Run `test_algorithm_meta_drift` (verifies exact sidecar reproducibility).
3. Run `test_temporal_workspace` & `test_temporal_core` (verifies roundtrip preservation and parity).
4. Run `test_spatial_tools` & `test_mcp_server`.
