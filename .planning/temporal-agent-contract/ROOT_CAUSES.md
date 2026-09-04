# Root Causes: Tool Schemas, Scene Semantics & Metadata Drift

## 1. Root Cause 1: Missing Root Object Type on Workspace Tool Schemas
- **Mechanism**:
  When authoring the 5 `temporal:*` workspace tools in `temporal_workspace_tools.cpp`, the author initialized `Json::Value schema(Json::objectValue)` and set `schema["properties"] = props`, but failed to assign `schema["type"] = "object"`.
- **Architectural Gap**:
  Neither `SpatialToolRegistry` nor `AgentTool` had a normalization or validation step to enforce that `inputSchema["type"] == "object"`.
- **Consequence**:
  `AgentTool::toOpenAiToolDefinition()` and `toMcpToolDefinition()` directly passed the un-typed schema, breaking downstream clients (OpenAI API, Claude/MCP, Pi) that require object schema definitions.

## 2. Root Cause 2: Intent-Corrupting Loss of Scene Semantics in `register_collection`
- **Mechanism**:
  `TemporalRegisterCollectionTool::execute` parsed `input["scenes"]` by only extracting string `path` and string `time`, discarding `bands`, `mask_band`, `quality_band`, and multimodal tags, then called `TemporalCollection::fromScenePaths`.
- **Architectural Gap**:
  Lack of a single canonical scene-parsing function. `fromInlineScenes` (in `rs_temporal_collection_input.cpp`), `collectionFromInput` (in `temporal_collection_tools.cpp`), and `TemporalRegisterCollectionTool::execute` each had independent parsing logic.
- **Consequence**:
  Scientific users or Agents supplying band overrides or cloud mask bands had their parameters silently ignored upon collection registration.

## 3. Root Cause 3: Permissive, One-Way Algorithm Metadata Drift Testing
- **Mechanism**:
  `tests/test_algorithm_meta_drift.cpp` only asserted `store.loadDefaults() >= 6` and skipped missing adapters with a warning. It never checked whether newly added task-declaring algorithms in C++ were missing sidecars on disk.
- **Architectural Gap**:
  The generation logic was embedded inside `src/cli/main_cli.cpp` rather than exposed as a library function in `AlgorithmMetaStore`, making in-process regenerate-and-compare tests impossible.
- **Consequence**:
  Discrepancies between in-code operator declarations and shipped metadata files could silently pass test gates.
