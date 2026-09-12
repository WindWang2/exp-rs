# ARCHITECTURE — Contract Platform 9.0

## Design principles

1. **Registries stay authoritative.** `RSOperatorRegistry`, CommandRegistry,
   help content store, model catalog, capability knowledge, plugin manifests
   remain the single sources. This track adds **projections and guards**, not
   a second metadata store.
2. **Derive, don't duplicate.** The canonical descriptor (M1) is *projected
   at test time* from live registries; nothing hand-copied. The only
   committed artifacts are generated snapshots under `data/contracts/` used
   for byte-compare mutation resistance.
3. **Fail loud, fail specific.** Every guard names the node, the edge, the
   owning file, and the exact remediation (regenerate command or whitelist
   entry with reason).
4. **No cross-track rewrites.** Findings that belong to other tracks become
   failing tests + REVIEW_LOG entries.

## Component map (new code)

```
src/contracts/                      (new lib: sicnu_contracts, header-only-ish, no Qt)
  contract_descriptor.{h,cpp}     M1 canonical typed descriptor + JSON projection
                                  (schema exp.contract.descriptor.v1), round-trip
  operator_param_scanner.{h,cpp}  M2 source scanner: declared-vs-read param extraction
  contract_graph.{h,cpp}          M0 node/edge inventory + findings report
  command_ref_scanner.{h,cpp}     M3 command/action id extraction from app sources
  error_code_scanner.{h,cpp}      M3 error enum census from framework headers
tests/
  test_contract_platform_9.cpp    M0 graph integrity on the live tree
  test_contract_projection_9.cpp  M2 impl↔schema equality + surface projections
  test_command_contract_9.cpp     M3 command/help/action reference graph
  test_diagnostics_contract_9.cpp M3 error-code census vs diagnostics catalog
  test_capability_contract_9.cpp  M4 capability/tool/model/mapspec/plugin floors
  test_contract_mutation_9.cpp    mutation tests (deliberate drift must be caught)
  benchmark_contract9.cpp         M8 generation-cost benchmark
data/contracts/
  contract_graph.snap.json        committed snapshot (byte-compare target)
docs/verification/CONTRACT_PLATFORM_9.md
```

The scanner and graph code are plain C++17/20 + std::filesystem + jsoncpp
(no Qt) so they are portable and usable headless; tests link the live
registries like existing drift tests do (CMAKE_SOURCE_DIR for source/data
access, repo precedent `test_capability_drift.cpp`).

## M0 contract graph

Node kinds: `operator`, `command`, `help_topic`, `diagnostic`, `error_code`,
`tool`, `capability_entry`, `algorithm_meta`, `model`, `recipe`, `mapspec_ref`,
`plugin_contribution`.

Edges (all derived, each with consumer file evidence):
- tool → operator (tool_catalog providers)
- help_topic `command.<id>` → command
- preflight_action → command
- empty_state_cta → command
- surface_action_ref (ribbon/menu/palette/window lookups) → command
- diagnostic → error_code (curated diagnostics.json coverage)
- capability_entry → operator
- recipe → operator; mapspec_ref → template/component/style
- plugin_contribution → operator/command

Findings vocabulary: `missing_node`, `dangling_ref`, `duplicate_id`,
`phantom_help`, `phantom_capability`, `undocumented_param`, `enum_drift`,
`undeclared_read` (M2), `dead_schema_param` (M2).

Serialization: `exp.contract.graph.v1` JSON, nodes+edges+findings, sorted,
stable ordering → snapshot byte-compare.

## M1 canonical descriptor (minimal, extensible)

```cpp
struct ContractParam { std::string name, type, description; bool required;
                       std::string defaultValue; std::vector<std::string> enumValues;
                       bool hasRange; double minimum, maximum; };
struct ContractDescriptor {
  std::string id, kind, title, description;
  std::vector<ContractParam> params;
  std::vector<std::string> outputs, requiredParams, errorCodes;
  std::string commandId, determinismGrade, helpTopicId;
  std::vector<std::string> capabilityTags;
  Json::Value extensions;   // domain-specific passthrough (x-rs-* etc.)
};
```

`ContractDescriptor::fromOperatorSchema(Json::Value)` is the authoritative
projection; other surfaces (MCP tool schema, CLI listSchemas, SchemaForm
source, algorithm_meta) are *compared against* it, never merged into it.

## M2 impl↔schema equality (the headline guard)

Scanner, per operator source file under `src/operators/**`:

- **Declared extraction** from the `schema()` body: every `makeXxxParam("name"…)`
  call and every `props["name"] = …` / `params["name"] = …` object-key
  assignment on the local schema-building variables.
- **Read extraction** from the whole implementation file: reads through the
  execute param variable (`params["k"]`, `params.isMember("k")`,
  `requireString(params,"k")`, `getString/getInt/getDouble/getBool/getEnum/
  hasNumber/getStringArray(params,"k")`), with alias resolution for
  `const Json::Value& alias = params;`. Nested-object reads (`manifest["x"]`)
  are only attributed when the base variable is the params root — nested
  sub-object keys are out of scope (schema declares sub-schemas separately).
- **Comparison**: `undeclared_read` = read key absent from schema params →
  FAIL (this is exactly #872/#879/#880). `dead_schema_param` = schema param
  never read in the file → FAIL unless allow-listed (params consumed by
  generic runners, forward-compat declared keys) with reason.
- **Honesty**: unknown/unparseable constructs produce `scan_unresolved`
  findings that FAIL the test (no silent pass). File-scoped allow-lists live
  in the test as explicit `{file, key, reason}` records.

Mutation evidence: (a) unit-test scanner on synthetic sources with planted
drift; (b) copy a real operator source into a temp dir, delete one
`makeXxxParam` line, re-scan → must report the deletion. This is the
"delete a field → suite red" requirement, without mutating the real tree.

Surface projections compared per operator id:
`RSOperatorRegistry::listSchemas()` ↔ per-operator `schema()` (identical
source); MCP `tool_catalog` inputSchema param sets ⊇ operator schema params
for operator-backed tools; algorithm_meta ids resolve in registry.

## M3 command / help / diagnostics

- Command id set extracted from *all* registration sites
  (`RS_CMD(`/`base(` literals in `src/app/workbench/command_defs.cpp` plus
  `registerCommand` sites in workbench panels/windows — extraction via
  string-literal scan with documented idiom list).
- Reference sets extracted from consumers: preflight suggested actions,
  `selection_context.cpp` commandIds, `m_commandRegistry->action("…")`
  lookups, help `command.*` ids, empty-state CTA definitions.
- Guards (each closes a named issue class): help ↔ registry (#869),
  action refs → registry (#881, #882), phantom help topics (#869 inverse),
  duplicate help ids across all data/help files (#871 adjacency),
  HelpContentStore single-load + duplicate rejection regression (#871),
  error-code census ↔ curated diagnostics pages with explicit allow-list so
  `DiagnosticCatalog::fallback()` cannot silently absorb new codes (#870).
- Shortcut ownership stays with CommandRegistry's registration-time
  uniqueness + existing `test_shortcut_conflicts` (reused, not rewritten).

## M4 capability knowledge

Extend the drift direction with floors + generated-index byte-compare:
every registered operator appears in ≥1 of {tool catalog, capability
knowledge, allow-list}; model catalog modalities agree with knowledge
entries; recipe/mapspec/plugin references resolve. Snapshot
`data/contracts/contract_graph.snap.json` byte-compares against the live
graph; regeneration is one documented command; any diff forces a conscious
contract update (this is the desired friction).

## Ladder / readiness integration (M5, M10)

Add the six new test executables to L2 (`contracts-9` items) and a contract
section to `collect_readiness.py` output (graph findings count, snapshot
freshness). Statuses stay honest (`not-built` never `pass`).

## Failure / remediation UX

Every failure message: kind, node id, consumer evidence `file:line`,
remediation ("add schema param", "add help entry id X", "regenerate
snapshot: `python3 scripts/contract_regenerate.py`"). A small
`scripts/contract_regenerate.py` regenerates `data/contracts/` snapshots
offline.
