# ORACLES — ds41-capability-search-13

Objective, executable acceptance conditions. Each maps to a concrete test
or command. "2×" = passes on two consecutive runs.

## Track-specific oracles

- [ ] **O1 — filters are real.** `search_algorithms` honors `tag`, `tags`
      (AND), `purpose`, `task` (taskFamily), `modality`/`modalities`,
      `band_roles`, `input_type`, `output_type`, `large_raster_safe`,
      `deterministic`, `gpu`, `memory_policy`, `cost_class`, `temporal`,
      `group`, `query` — every advertised filter measurably narrows or
      reorders results on the live catalog (golden corpus test).
- [ ] **O2 — CLI/MCP parity.** For a fixed battery of queries (text-only,
      tag-only, tag+text, task, modality, io-kind, combined, zero-hit),
      CLI `algorithms search --json` and MCP `search_algorithms` return
      the **same id sequence** (same matcher, same ranking, same
      tie-break) — proven by a test that runs both surfaces.
- [ ] **O3 — deterministic ranking.** Same query twice → identical id
      sequence; equal scores tie-break by id; empty query returns the
      deterministic catalog order.
- [ ] **O4 — bounded+typed.** `limit` clamps to the documented cap
      (1..500, default 50); `cursor` paginates with `nextCursor=-1` at
      the end; unknown filter keys on the CLI are typed refusals
      (`InvalidInput`); a 1 MiB query string and absurd `limit` cannot
      produce unbounded allocation (the engine never copies the catalog
      per candidate — results are references/ids).
- [ ] **O5 — zero-result honesty.** A no-hit query returns an empty
      `algorithms` array plus a `suggestions`/`vocabulary` payload
      (available groups / task families / tags / modalities / port types)
      and nearest-by-prefix/score ids — never an error, never invented
      ids.
- [ ] **O6 — schema/help truth.** `tools/list` + `get_tool_schema` for
      `search_algorithms` declare exactly the implemented keys; CLI
      `algorithms search` usage text names the same flags. A parity test
      compares the meta-protocol property set to the engine's supported
      field set (drift = red).
- [ ] **O7 — mutation potency.** Deleting a tag check, ignoring the
      purpose filter, swapping AND→OR on `tags`, or dropping the
      deterministic tie-break each turn the golden corpus gate RED
      (proven by injecting each mutation once during development).
- [ ] **O8 — generated surfaces idempotent.** No hand-edited JSON; the
      track adds no metadata fields → `--export-catalog` /
      `capability_knowledge_tool gen-meta|gen-pages --check` diff = 0 (2×).

## Shared gates (must stay green, 2×)

- `test_algorithm_meta_drift`, `test_capability_knowledge`,
  `test_capability_completeness`, `test_capability_surface_parity`
- `test_mcp_server` (search-related sections),
  `test_agent_tool_catalog`, `test_catalog_pagination`,
  `test_surface_parity` (tool-surface projection), `test_cli_commands_json`
- `git diff --check` clean modulo the documented generated-JSON
  trailing-space contract (none expected — this track generates nothing).

## Common oracles (global protocol)

- [ ] Worktree from latest `origin/master` (`79adfe78a`) — done, recorded
      in BASELINE.
- [ ] Latest PRs/issues/branches read + deduped — BASELINE/DEDUP.
- [ ] Core changes inside OWNERSHIP — `git diff --stat` evidence.
- [ ] Targeted build succeeds (`build-cap`, `-j2` cap).
- [ ] Core tests pass **twice consecutively**.
- [ ] ≥1 new test proven to catch an injected defect (O7).
- [ ] Independent review: P0=0, P1=0; fixes re-verified twice.
- [ ] Pre-PR fetch + overlap scan; worktree clean; PR opened (no merge).
