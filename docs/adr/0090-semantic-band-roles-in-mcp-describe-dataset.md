# ADR 0090: Semantic Band Roles in the MCP `describe_dataset` Tool

## Context

The Agent / MCP integration goal (E3) wants the agent to operate on semantic
remote-sensing concepts — "select the NIR and Red bands", "mask cloudy pixels"
— rather than band numbers and file paths. ADR 0087 added band roles to the
`WorkspaceSnapshot` system prompt (the Copilot channel), but the MCP channel
still described bands as `index` / `color_interpretation` / `dataType` /
`nodata_value` only: `describe_dataset` gave the agent no way to know which
band is NIR without guessing from the index.

## Decision

`McpServer::handleDescribeDataset` (the `describe_dataset` tool) now emits a
`role` field per band, read from the product's `SICNU_BAND_ROLE` GDAL band
metadata via `GdalDatasetWrapper` (the same seam the QA-mask dialog uses).
Bands without product semantics report an empty string; the rest of the band
metadata is unchanged. The change is purely additive to the JSON contract.

## Consequences

- The agent can now resolve "NIR / Red / QA" from `describe_dataset` output
  and drive band-role-aware operator calls (e.g. `rs:spectral_index` NDVI
  without band numbers) through the MCP channel — the same semantic layer the
  GUI uses.
- Plain rasters keep their previous description (empty roles), so existing
  MCP clients are unaffected.
- A new MCP test pins the role emission for a two-band product raster
  (`nir`, `red`) at the handler seam; the full MCP suite stays green.
