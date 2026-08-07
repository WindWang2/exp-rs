# ADR 0087: Semantic Band Roles in the Agent Workspace Snapshot

## Context

The E3 mission slice asks the Agent to operate on semantic remote-sensing
concepts ("select the NIR and Red bands") instead of raw band numbers and file
paths, and to expose product/semantic metadata in workspace snapshots. The
product model already assigns semantic band roles (`BandRole`, ADR-0065 / A3);
`RasterBandStructure.role` carries them through asset discovery and the
`SICNU_BAND_ROLE` GDAL metadata. But `WorkspaceSnapshot` — the text the agent's
system prompt sees for loaded assets — only exposed `bandCount`, so the agent
could not tell which band is NIR without falling back to band numbers.

## Decision

`DataAssetInfo` gains `QStringList bandRoles` — one stable id per band in band
order (`"nir"`, `"red_edge"`, `"qa"`, ...), `""` for bands with no known role.
`WorkspaceSnapshot::capture()` fills it from `RasterStructure::bands`, and
`toSystemPromptHeader()` renders it as `4 bands (roles: nir, red, green, blue)`
whenever the list is non-empty. Plain rasters without product semantics keep
their current rendering (band count only) and a `bandCount == 0` asset is
unaffected — the change is purely additive to the prompt text.

## Consequences

- The agent can select bands by role for the standard optical workflows
  (NDVI, classification, change detection) on product-imported assets without
  band-number guessing; unknown roles stay visible as `unknown` so the list
  remains aligned with band order.
- The snapshot stays a read-only projection: it consumes `RasterStructure`,
  never writes back, and the prompt format is covered by the existing pure
  serialization tests plus a new capture-level test with a 4-band role-carrying
  structure (54 assertions across 5 test cases).
- Future E3 tool contracts (e.g. "calculate NDVI on asset X") can resolve bands
  by role from the snapshot instead of requiring the agent to reason about
  arbitrary band indices.
