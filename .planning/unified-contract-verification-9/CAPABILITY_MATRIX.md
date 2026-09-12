# CAPABILITY_MATRIX — does master already have it?

For each capability planned by the goal: master status at `f316dfdbb4`,
evidence, and the delta this track adds. Rule: no re-implementation of
capabilities that exist and work — extend or project only.

| Capability | In master? | Evidence | Delta (9.0) |
|---|---|---|---|
| Operator registry + schema() JSON | YES | `src/operators/framework/rs_operator_registry.h`, `rs_schema.h`, `listSchemas()` | read as authoritative source |
| CLI operator surface | YES | `src/cli/cli_commands.*`, `--operator-help` etc. via `help_cli_projections.cpp` | projection target; no CLI rewrite |
| MCP tool catalog + get_tool_schema | YES | `src/agent/mcp_server.h` (tools/list, searchTools, getToolSchema), `src/agent/tool_catalog/*` | projection target |
| SchemaForm | YES | `test_schema_form_4.cpp`, workbench 8.0 | projection target (source-scan/form builder vs schema) |
| algorithm_meta sidecars | YES | `src/processing/framework/algorithm_meta_store.*`, `test_algorithm_meta_drift` (byte-compare) | extend: replace brittle count pin with derivation; feed graph |
| Harness capability knowledge | YES | `data/agent/capabilities`, `test_capability_drift.cpp` | extend: generated-index byte-compare, floor for operators/tools/models |
| Model catalog | YES | `src/operators/framework/model_catalog.{h,cpp}`, `test_model_catalog_v2.cpp` | projection target (modalities vs capability knowledge) |
| Plugin contributions | YES | `src/plugins/**`, protocol 1.1, conformance 2.0 | read as authoritative source (manifest-declared ops/commands) |
| MapSpec / cartography tools | YES | `src/agent/mapspec/`, `src/agent/cartography/` | projection target (template/component/style refs) |
| Error code enums | YES | `rs_operator_error.h`, `harness_error.h`, `model_runtime.h`, `worker_protocol.h` | census target for diagnostics coverage |
| Help content store / registry / search | YES | `src/help/**`, `data/help/*.json` | coverage graph + single-load regression (M3) |
| DiagnosticCatalog with fallback | YES | `src/help/diagnostic_catalog.h` | census guard so fallback cannot hide drift (M3) |
| CommandRegistry + shortcut uniqueness | YES | `src/app/workbench/command_registry.h` (duplicate id/shortcut rejected) | reference-graph guard across consumers (M3) |
| Verification ladder L0-L8 | YES | `scripts/verification_ladder.py` | additive lanes + statuses (`not-run` honesty already present) |
| Readiness report | YES | `scripts/collect_readiness.py` | additive contract section |
| Contract fuzz suites | YES | `tests/test_contract_fuzz_{agent,data,io,ipc,lang,ops}.cpp` | reuse; new fuzz only for descriptor round-trip |
| **Impl-accepted-params ↔ schema equality** | **NO** | the #872/#879/#880 fixes were manual | **BUILD (M2) — headline** |
| **Canonical typed descriptor shared across projections** | **NO** | each projection re-parses raw JSON | **BUILD (M1)** |
| **Contract graph w/ missing-node/dangling-ref/duplicate-id report** | **NO** | checks scattered in ~30 tests | **BUILD (M0)** |
| **Mutation tests proving guards go red** | **NO** | none found | **BUILD (M2/M3/M4)** |
| Command reference graph across all consumers | PARTIAL | test_help_coverage scans command_defs.cpp only | extend (M3) |
| Error-code census vs diagnostics | PARTIAL | test_help_coverage checks *declared* codes resolve; fallback hides new codes | extend (M3) |
| Generated projection byte-compare (help/commands data) | PARTIAL | algorithm_meta only | extend (M3/M4) |
| Sanitizer/fault lanes | YES (L5) | ladder | reuse, add bounded ASan lane item for contract tests |
| Benchmark governance | YES (L7 + perf_report.py) | 8.0 | add contract-generation cost benchmark (M8) |
| Visual/artifact verification | YES (L6) | 8.0 | reuse; contract platform adds no renderers |
