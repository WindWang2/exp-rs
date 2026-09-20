# BASELINE — ds41-capability-search-13 (Track I: Capability Discovery/Search Semantics)

## Live state at track start (2026-09-20, all facts fetched live)

- `origin/master` = `79adfe78a16b9419eef180cf9e6e5739658621a2`
  (Merge PR #1134 flash-plugin-sdk-12, committed 2026-09-20T13:39:35Z; verified via
  `gh api repos/WindWang2/exp-rs/branches/master` — the local remote-tracking ref is
  unstable because the worktree fleet's janitor mutates it).
- Open PRs (1): **#1135** `agent/flash-temporal-phenology-12` — Temporal Phenology 12.0.
  - Files overlapping this track's domain: `src/agent/harness/capability_catalog.cpp`
    (one `familyMap` line: `rs:temporal_sar_fusion` → `temporal`),
    `src/cli/cli_commands.cpp` (a disjoint include + comma fix already on master),
    `tests/CMakeLists.txt` (adds `test_temporal_irregular`),
    `tests/test_algorithm_meta_drift.cpp` (pin 43→53 — master already carries 53),
    `tests/test_contract_cross_surface_11.cpp`, plus generated
    `data/processing/algorithm_meta{,/capability}/*.json` + `pi/knowledge/*.md`.
  - No overlap with the search engine, MCP handler, meta-protocol schema, or CLI
    `algorithms search` matcher in the regions this track edits.
- Merged PRs of record:
  - **#1127** `track/ds41-capability-help-sync` — repaired merge-corrupted capability
    metadata + built the registry→sidecarA→sidecarB→help→CLI/MCP parity/completeness
    gates. **Explicitly left as out-of-scope:** "the `search_algorithms`
    documented-but-unimplemented tag/purpose filters (production surface behavior)".
    This track implements exactly that gap.
  - **#1020** `zcode/cli-mcp-agent-surface-11` — one discovery projection for tools;
    `search_tools` gained the rich facet engine (`AgentToolCatalog::searchTools`).
- Open issues: none (`gh issue list` empty).
- Remote `agent/*`, `track/*`, `fix/*` branches: all merged residue except
  `agent/flash-temporal-phenology-12` (PR #1135). `track/ds41-capability-help-sync`
  remote branch deleted after merge; its worktree directory is reclaimed here
  (see DECISIONS D1).

## Search-surface facts on master @ 79adfe78a

Four different matchers exist today:

| Surface | File | Matcher | Filters honored |
|---|---|---|---|
| MCP `search_algorithms` | `src/agent/mcp_server.cpp:1489` `handleSearchAlgorithms` | ad-hoc filter over `handleListAlgorithms` output | `query` (CI substring over id/displayName/group/description — **purpose NOT searched despite docs**), `group` exact, `input_type`/`output_type` (port type CI), `large_raster_safe`, limit/cursor. **No `tag`, `purpose`, `task`, `modality` params.** |
| CLI `algorithms search` | `src/cli/cli_commands.cpp:173` `commandAlgorithms` | case-**sensitive** substring over id/displayName/description/group | only a positional needle — no flags at all |
| MCP `search_tools` | `src/agent/tool_catalog/agent_tool_catalog.cpp:390` | `SearchQuery` engine: scored ranking + facet AND-filters | text, group, tag, input/output type, largeRasterSafe, taskFamily, deterministic, gpu, temporal, memoryPolicy, costClass, modality/modalities, bandRoles |
| CLI `tools search` | `src/cli/cli_tool_commands.cpp:91` `matchesQuery` | case-sensitive substring on name/description/family | needle + `--family` + `--source` |

Documented vs implemented:
- `meta_protocol_tools.cpp:26-36` `search_algorithms` description advertises
  "group, tag, purpose text, input or output type, or large-raster safety" — the
  declared input schema has no `tag`/`purpose`/`task`/`modality` property and the
  handler honors none of them. `query` doc says it matches "id, name, group and
  purpose" — code searches `description`, not `purpose`.
- ADR 0120 §9 advertises `search_algorithms` "(group/tags/input/output/…)".

Authority (from `.planning/ds41-capability-help-sync/AUTHORITY_MAP.md`, verified live):
- Single source of truth = live `AlgorithmDescriptor`/`AgentMetadata`
  (`src/processing/framework/algorithm_descriptor.h`) built from
  `RSOperator::metadata()` + `schema()` (+ `ProviderAlgorithmAdapter::buildDescriptor`
  for provider algorithms).
- Honest fields available for search: `id`, `displayName`, `group`, `description`,
  `agentMetadata.{purpose, tags, taskFamily, memoryPolicy, deterministic,
  gpuAccelerated, costClass, largeRasterSafe, determinismGrade, accuracy, notes}`,
  `inputs/outputs[].type` + `rsContract` (`dataKind`, `modality`, `modalities`,
  `bands[].role`).
- Universe: 152 `rs:` operators + provider/plugin algorithms (53 declare taskFamily).

## Worktree

- Dir: `exp-rs-worktrees/ds41-capability-help-sync` (reclaimed residue of merged
  `track/ds41-capability-help-sync`; its `build-cap/` dir keeps the
  vcpkg + Qt 6.8 + MSVC 14.38 + Ninja toolchain warm — see DECISIONS D1).
- Branch: `agent/ds41-capability-search-13` created at `79adfe78a`
  (`git merge-base HEAD <base>` = `79adfe78a` — exact base, zero carry-over code).
