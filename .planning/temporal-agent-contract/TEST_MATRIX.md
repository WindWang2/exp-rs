# Test Matrix: Contract Verification

| Target / Suite | Invariant Under Test | Expected Behavior | Actual Outcome | Assertions Passed |
| :--- | :--- | :--- | :--- | :--- |
| **`test_agent_tool_catalog`** | OpenAI `parameters.type == "object"` | All exported OpenAI function definitions must have `"type": "object"`. | **PASS** | 2054 assertions (9 test cases) |
| **`test_agent_tool_catalog`** | MCP `schema.type == "object"` | All exported MCP tool definitions must have `"type": "object"`. | **PASS** | Included above |
| **`test_agent_tool_catalog`** | Fail-Fast Normalization | Non-object schema or illegal type throws `std::invalid_argument`. | **PASS** | Included above |
| **`test_agent_tool_catalog`** | Temporal Tool Invariant | All 5 workspace tools have valid object root schemas. | **PASS** | Included above |
| **`test_algorithm_meta_drift`**| Exact Membership | Shipped sidecar set matches task-declaring descriptors exactly (cardinality == 6). | **PASS** | 200 assertions (1 test case) |
| **`test_algorithm_meta_drift`**| Byte-for-Byte Reproducibility| Generated in-memory catalog matches on-disk files byte-for-byte. | **PASS** | Included above |
| **`test_algorithm_meta_drift`**| Hard Fail on Unresolvable | Sidecar with unresolvable ID fails the test instead of warning. | **PASS** | Included above |
| **`test_algorithm_meta_drift`**| Export Roundtrip | `exportCatalog` to temp dir produces byte-identical sidecars. | **PASS** | Included above |
| **`test_temporal_agent_tools`**| Semantic Preservation | `temporal:register_collection` preserves `bands`, `mask_band`, `quality_band`, and multimodal metadata. | **PASS** | 221 assertions (5 test cases) |
| **`test_temporal_agent_tools`**| Roundtrip Fidelity | Descriptor save and reload preserves 100% of scene metadata. | **PASS** | Included above |
| **`test_temporal_agent_tools`**| Workspace/Inline Parity | `fromInlineScenes` produces identical `TemporalSceneRef` as registered workspace collection. | **PASS** | Included above |
| **`test_temporal_agent_tools`**| Negative Validation | Empty scenes, missing path, bad date, bad bands fail closed. | **PASS** | Included above |
| **`test_temporal_workspace`** | Workspace Lifecycle | Collection registration, asset binding, revision bump, cache clearing. | **PASS** | 138 assertions (11 test cases) |
| **`test_temporal_core`**      | Temporal Core Math | Time parsing, sorting, duplicate resolution, preflight checks. | **PASS** | 367 assertions (6 test cases) |
| **`test_mcp_server`**         | MCP Compatibility | `tools/list`, `get_tool_schema`, `search_tools` output normalized schemas. | **PASS** | 2751 assertions (18 test cases) |
| **`test_spatial_tools`**      | Spatial Tool Providers| Execution and schema contracts for all spatial providers. | **PASS** | 139 assertions (16 test cases) |
| **`sicnu_geo_rs_cli`**        | CLI Export | `sicnu_geo_rs_cli --export-catalog` writes byte-identical sidecars. | **PASS** | 6 sidecars generated, 0 diff |
