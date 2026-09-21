# OWNERSHIP — ds41-capability-search-13

## Owned (this track may modify)

- `src/processing/framework/algorithm_search.{h,cpp}` — NEW: canonical
  query model + matching/ranking engine over `AlgorithmDescriptor`.
- `src/agent/mcp_server.{h,cpp}` — `handleSearchAlgorithms` rewired onto
  the engine; `tools/call` argument parsing for the new filter keys.
- `src/agent/tool_catalog/meta_protocol_tools.cpp` — `search_algorithms`
  input schema + description made truthful (implements `query`, `group`,
  `tag`, `purpose`, `task`, `modality`, `input_type`, `output_type`,
  `large_raster_safe`, `limit`, `cursor`; full `dataTypeToString`
  vocabulary documented — no unimplemented filters advertised).
- `src/cli/cli_commands.cpp` — `commandAlgorithms` `search` subcommand:
  flag parsing + engine call + shared projection helpers.
- `src/processing/CMakeLists.txt` — new engine sources.
- `tests/test_algorithm_search.cpp` — NEW: golden query corpus, mutation
  potency, malformed/oversized handling, vocabulary echo, differential
  ranking, provider-union coverage, and CLI↔MCP same-query parity
  (id set + order, real CLI subprocess; parity folded in here rather
  than a separate `test_capability_search_parity.cpp`).
- `tests/CMakeLists.txt` — registration of the new test (main-agent
  only edit; conflict hotspot with PR #1135 — resolve by appending after
  its block if it lands first).
- `docs/` — a search-semantics section in the capability/agent docs that
  referenced the unimplemented filters (precise files decided during impl;
  candidate: `docs/agent/operator-development-template.md`, ADR 0120 note).
- `.planning/ds41-capability-search-13/*.md`, `.gitignore` whitelist line,
  `.goal-loop-ledger.md` appended section.

## Shared / read-only projection (allowed only via generators)

- `data/processing/algorithm_meta/**/*.json`, `data/agent/capabilities/**`,
  `pi/knowledge/**` — owned by the generation pipeline; this track adds NO
  new fields, so no regeneration is expected. If a gate asks for it, use
  `sicnu_geo_rs_cli --export-catalog` / `capability_knowledge_tool gen-meta
  | gen-pages`, never hand-edits.

## Forbidden (parallel domains)

- `src/processing/algorithms/**`, `src/operators/**` implementation files
  (algorithm semantics; #1135 adds temporal operators there).
- `src/agent/harness/capability_catalog.cpp` familyMap region (owned by
  #1135's one-line addition — this track does not need it).
- Mission tools, model runtime, workflow engine, dataset/experiment.
- `tests/test_algorithm_meta_drift.cpp` (pin owned by #1135).

## Conflict hotspots

- `tests/CMakeLists.txt` — every track appends `sicnu_add_test`; append
  near the capability-gate block, resolve on rebase if #1135 lands first.
- `src/cli/cli_commands.cpp` — #1135 adds an include at line ~58 and the
  `"tools","batch"` fix (already on master); this track edits
  `commandAlgorithms` (~line 173) — disjoint regions.
