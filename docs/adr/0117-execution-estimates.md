# ADR 0117: Execution-Resource Estimates in Agent Metadata

## Context

The DoD large-raster contract requires every major raster operator to declare
its memory policy (done, ADR 0073) and to "expose useful execution metadata
where appropriate: memory policy, tile size, estimated RAM, temporary disk
usage". The memory policy was already surfaced in the agent metadata; the
tile/RAM/disk estimates were not.

## Decision

- New virtual `RSOperator::executionEstimate()` (base default: empty object =
  unknown/auto). Overrides declare `tileWidth` / `tileHeight` /
  `estimatedRamBytes` / `temporaryDiskBytes` (0 = unknown/auto), grounded in
  the operator's `memoryPolicy()` and the actual `run()` implementation
  (typical input: 1024×1024, float32 = 4 B/pixel).
  - Streaming / MultiPassStreaming operators declare their real block size
    (e.g. apply_mask's 256-row strips, post_classification_change's 256-row
    passes, atmospheric_correction's 256² tiles) and a tile-proportional RAM
    estimate plus global state.
  - FullRaster operators declare no tile and a whole-raster RAM estimate
    (input bands + working buffers + output buffers).
  - ExternalProcess operators declare the in-process footprint and only
    `temporaryDiskBytes` when a temp file is actually written.
- `AgentMetadata` gains an `execution` field (JSON, serialized/parsed like the
  other fields), and `RsOperatorAdapter::buildFromRsOperator` merges the
  estimate under `meta["execution"]` next to `memoryPolicy` — so the MCP /
  Agent / workspace surfaces see both without a schema change.

## Consequences

- The DoD execution-metadata item is closed: every `rs:` operator carries a
  memory policy, and ~31 operators now quantify tile size and peak RAM (disk
  where real) through the same descriptor path the Agent already consumes.
- Estimates are declarations for UX/planning ("this step may need ~48 MiB"),
  not runtime measurements; they are documented as typical-input values.
- Pinned by `test_atomic_algorithm_registry`: rx_anomaly (full_raster, RAM,
  no tile) and apply_mask (streaming, tile) surface their estimates, and every
  registered `rs:` operator still declares a memory policy.
