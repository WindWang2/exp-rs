# ADR 0112: MCP/Agent Provenance Queries (`get_lineage`)

## Context

The DoD provenance contract requires that provenance be accessible to the UI
(done — Data Manager panel, slice 23), to experiment/report export (done —
`RSOperationLogger`), and to the **Agent/MCP surface**, which was missing. The
Data Manager already exposes the lineage queries (`derivedFrom` /
`derivedOutputsOf`, slice 9) and per-asset `provenance` records
(`DerivationRecord` with algorithm id, parameters, task reference and
completion time), but no MCP tool surfaced them: the agent could see band
roles via `describe_dataset` but could not answer "what produced this asset,
with what parameters, and what did it feed?".

## Decision

- New MCP tool `get_lineage(asset_id)`: resolves the Data Manager asset by
  UUID and returns:
  - the asset identity (`id`, `name`, `source`),
  - `provenance` — the deriving `DerivationRecord` as JSON (algorithm id,
    parameters, inputs, task reference, timestamps) when the asset was
    produced,
  - `derivedFrom` — input assets (id + display name),
  - `derivedOutputsOf` — assets derived from it (id + display name).
- `McpServer` stores the injected `DataManager*` (previously it was only
  forwarded to `ToolCallDispatcher`), so the handler queries the same asset
  authority the dispatcher commits outputs through — one provenance source of
  truth.
- Malformed ids and unknown assets fail with actionable `std::runtime_error`
  messages, consistent with the other MCP handlers.

## Consequences

- The agent can now trace the processing lineage end-to-end: find an
  analysis-ready asset, ask what produced it and with which parameters, and
  discover downstream products — completing the DoD "provenance accessible to
  Agent/MCP" requirement without duplicating the model (MCP reuses
  `DerivationRecord::toJson` and the Data Manager queries).
- Pinned by `test_mcp_server` `get_lineage` case: derived asset reports
  provenance + inputs; input asset reports outputs; malformed/unknown ids
  fail with actionable errors.
