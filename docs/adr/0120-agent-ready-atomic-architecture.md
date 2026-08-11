# ADR 0120 — Agent-Ready Atomic Algorithm Architecture

- Status: Accepted (2026-08-12)
- Scope: `src/processing/framework`, `src/operators/rs`, `src/agent`

## Context

The platform exposes remote-sensing capability to GUI / CLI / Workflow / MCP
and LLM agents through two registries (RSOperatorRegistry, AtomicAlgorithmRegistry)
and a projection layer that had drifted:

1. `AlgorithmDescriptor` hardcoded a single `output: Raster` port for every
   RS operator even though operator schemas already declare real outputs
   (statistics-only CSV, multi-output, string/numeric auxiliaries). Agents
   mis-planned: statistics-only operators were treated as raster producers
   and CSV/vector outputs were committed as raster assets.
2. Parameter validation was "required presence only" in ToolCallDispatcher;
   type/enum/range violations surfaced asynchronously after queueing.
3. Resource estimation was static-only (`executionEstimate()`), with several
   constants understating real working sets by up to ~50x.
4. Cancellation chains were broken in three places: the JobEngine fallback
   executor did not forward its cancel flag, the adapter-created context had
   no cancel wiring, and the workflow runtime had no cancellation at all.
5. Multi-function operators (`rs:change_detection`, `rs:image_fusion`) were
   monolithic selectors — not independently callable/composable primitives.

## Decision

**AtomicAlgorithmRegistry is the single Agent-facing canonical algorithm
catalog.** RSOperatorRegistry remains the low-level factory. All surfaces
(GUI copilot, CLI, MCP, workflow, ToolCallDispatcher) derive algorithm
capability from `AtomicAlgorithmRegistry` + `AlgorithmDescriptor`.

1. **AlgorithmDescriptor 2.0 — real ports.** Outputs are parsed from the
   operator schema's `outputs` object (`makeRootSchema`), mapping file formats
   to `DataType` (tif→Raster, csv→Table, shp/geojson→Vector, json→Json) and
   numeric/string auxiliaries to their types. Operators without declared
   outputs keep the single-Raster fallback (backward compatible).
2. **Port data contracts (`x-rs-contract`).** Machine-readable metadata on
   ports (bands min, gridRelation "same-grid", radiometricState list,
   dataKind, ...) consumed by preflight — not free-form prose.
3. **AgentMetadata 2.0.** Added structured planning hints actually consumed
   by preflight/discovery: `deterministic`, `sideEffects`, `idempotent`,
   `costClass`, `largeRasterSafe` (derived from memory policy),
   `supportsCancellation`, `producesProvenance`, `facadeOf`.
4. **Shared JSON-schema validation.** One `schema_validator` implementation
   (required / type / enum / range / array items / file shape / unknown
   parameter policy) with structured errors
   `{code, parameter, expected, actual, message}`, used by
   ToolCallDispatcher (unknown=Warn for legacy compatibility) and exposed as
   `ToolCallDispatcher::validateCall()` for MCP/preflight.
5. **Preflight.** `preflightAlgorithm(id, params)` validates schema, probes
   raster datasets (size/bands/CRS/radiometric state), checks same-grid/CRS/
   band/radiometric compatibility, and returns a dynamic RAM estimate —
   PLAN → PREFLIGHT → EXECUTE for agents.
6. **Dynamic resource estimates.** New seam
   `RSOperator::estimateExecution(params)` (default: static fallback);
   overflow-safe helpers in `resource_estimation.h`. Scheduler/preflight
   prefer the dynamic estimate.
7. **Cancellation contract.** `RSOperatorContext` gained a cancel callback
   (flag-first, callback fallback). JobEngine fallback executor forwards its
   flag; the adapter bridges caller `isCancelledFn` into the context;
   `WorkflowRuntime` gained per-session `requestCancel()`.
8. **Atomicization.** Change metrics split into `rs:change_difference`,
   `rs:change_normalized_difference`, `rs:change_ratio`, `rs:change_cva`,
   `rs:change_mad` plus reusable `rs:threshold_raster`; fusion methods into
   `rs:fusion_linear/brovey/pca/ihs/gram_schmidt`. All share one kernel
   (`rs_change_streaming`, `ImageFusion::processNativeFusion`); legacy
   selectors (`rs:change_detection`, `rs:image_fusion`) remain as facades and
   are marked via `facadeOf` metadata. Single-band change methods became
   tile-streaming (previously 5 full-scene buffers).
9. **MCP surface.** `list_algorithms`/`get_algorithm_schema` serve the
   canonical catalog; added `search_algorithms` (group/tags/input/output/
   large-raster-safe filters) and `preflight_algorithm`; execution status
   resolves the committed asset id so the execute→status→lineage loop closes.
   Catalog size is bounded by a regression test (compact discovery layer
   < 25% of full-schema injection).

## Consequences

- Agent-facing catalogs (copilot, MCP, CLI) now describe operators
  accurately; composition is validated by schema.
- Static estimates remain as fallback hints; operators that quantify dynamic
  estimates converge scheduler admission with real working sets.
- Legacy operator IDs and prompts keep working; the facades delegate to the
  shared kernels (no duplicated implementations).
- Numerical semantics of change detection / unmixing / resampling are
  preserved by golden/equivalence tests (streaming paths reproduce the
  full-scene math deterministically).

## Affected areas

`processing/framework/algorithm_descriptor.{h,cpp}`,
`schema_validator.{h,cpp}`, `algorithm_preflight.{h,cpp}`,
`resource_estimation.h`, `atomic_algorithm_adapter.{h,cpp}`,
`tool_call_dispatcher.{h,cpp}`, `atomic_algorithm_registry.{h,cpp}`,
`operators/framework/rs_operator.{h,cpp}`, `rs_operator_context.{h,cpp}`,
`workflow/workflow_runtime.{h,cpp}`, `agent/mcp_server.{h,cpp}`,
`operators/rs/rs_change_*`, `rs_fusion_aliases`, `rs_threshold_raster_operator`,
`rs_spectral_resample_operator`, `rs_spectral_unmixing` (kernel),
`rs_endmember_extraction_operator`, `rs_kmeans_operator`,
`rs_mosaic_operator`, `rs_atmospheric_correction_operator`,
`rs_spectral_index_operator`, `processing/algorithms/satellite_products.cpp`.
