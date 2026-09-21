# DEDUP — ds41-capability-search-13

## What is already implemented (do NOT redo)

- **Metadata authority + projection chain** (#1127, merged): registry →
  sidecar A (`data/processing/algorithm_meta/*.json`) → sidecar B
  (`capability/*.json`) → help topics → MCP/CLI surfaces, with drift,
  parity and completeness gates (`test_algorithm_meta_drift`,
  `test_capability_knowledge`, `test_capability_surface_parity`,
  `test_capability_completeness`). This track must keep all four green and
  must NOT hand-edit generated JSON.
- **`search_tools` facet engine** (#1020 + #701 facets, merged):
  `AgentToolCatalog::searchTools(SearchQuery)` over the *tool* catalog
  (algorithms + interaction + data tools) already implements tag/task/
  modality/band-role/memory-policy/cost-class/deterministic/gpu/temporal
  filtering + scored ranking + deterministic tie-break.
- **Pagination contract** (#701): `limit`/`cursor`/`nextCursor` envelopes,
  `mcpPageLimit` 1..500 default 50, already on `list_algorithms`,
  `search_algorithms`, `list_operators`, `tools/list`, `search_tools`.
- **Pi bridge** (`pi/exp-rs-spatial.ts`): pure MCP proxy — registers tools
  from `tools/list` schemas verbatim, forwards arguments untouched. No Pi
  matcher exists; MCP schema changes propagate to Pi automatically.

## The real gap this track closes

1. `search_algorithms` (MCP) ignores `tag`, `purpose`, `task`, `modality`
   and band-role facets entirely — advertised in its own description +
   ADR 0120 but absent from the input schema and the handler.
2. `search_algorithms.query` does not match `purpose` text despite the doc.
3. CLI `algorithms search` has **no filter flags**, is case-sensitive, and
   runs a third, unrelated matcher — CLI/MCP parity for the same query is
   impossible today.
4. No canonical query model: AND/OR semantics, normalization, empty query,
   unknown-filter behavior are undefined per surface (group is exact-match
   on MCP but substring on `search_tools`; CLI search is case-sensitive).
5. No ranking contract on `search_algorithms` (returns list order; MCP doc
   promises nothing, but the track requires stable ranking + deterministic
   tie-break shared with the CLI).
6. No zero-result guidance (no vocabulary echo / nearest ids).

## Dynamic dedup vs open PR #1135 (temporal phenology)

#1135 does NOT touch the search engine, MCP dispatch, meta-protocol table,
CLI `commandAlgorithms`, or any search test. Its `capability_catalog.cpp`
hunk is a one-line `familyMap` entry in a region this track does not edit;
its generated-metadata regeneration is re-runnable and non-conflicting
(generators are deterministic — whoever lands second re-runs them).
Its `test_algorithm_meta_drift.cpp` pin (43→53) is already on master.
Residual risk: `tests/CMakeLists.txt` registration block — both PRs add a
test in the same section; whoever merges second resolves a 3-line hunk.
Recorded in OWNERSHIP as a shared-file hotspot.

## Non-goals (recorded to avoid scope creep)

- No changes to `search_tools`/`AgentToolCatalog::searchTools` semantics
  (different surface, already facet-rich). The new algorithm-search engine
  *mirrors its filter vocabulary* but runs on `AlgorithmDescriptor`s.
- No operator implementations, no mission tools, no model runtime.
- No new metadata fields: the descriptor already carries every honest field
  the advertised filters need (tags, purpose, taskFamily, rsContract
  modality/bandRoles, port types, memoryPolicy, costClass, deterministic,
  gpuAccelerated, largeRasterSafe). If a filter would require guessing from
  prose it is excluded by design.
- No help-system/F1 consolidation (owned by help tracks).
