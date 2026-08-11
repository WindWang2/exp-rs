# Adding a New Remote-Sensing Operator (Agent-Ready Template)

SICNU GEO RS exposes algorithms through the **Atomic Algorithm Registry**
(`AtomicAlgorithmRegistry`) — the canonical Agent-facing catalog consumed by
the GUI copilot, CLI, MCP and workflow. New operators must be:
**single-responsibility, schema-validatable, preflightable, independently
callable/benchmarkable**, and described by real ports.

Follow the 9-step checklist below. Example companions to copy from:
`src/operators/rs/rs_change_primitives.*` (atomic metric), 
`src/operators/rs/rs_threshold_raster_operator.*` (reusable mask),
`src/operators/rs/rs_fusion_aliases.*` (facade alias over a shared kernel).

## Pipeline

```
kernel  →  RSOperator  →  RsOperatorAdapter  →  AtomicAlgorithmRegistry
                ↓                                     ↓
         schema()/metadata()/estimate()      TaskCenter/JobEngine
                                                     ↓
                              CLI / GUI / Workflow / MCP / Agent
```

## The 9 steps

1. **Kernel** (`src/processing/algorithms/`) — pure algorithm, no Qt UI,
   no GDAL orchestration unless that *is* the kernel. Tile/stream friendly
   (O(tile × bands + bands²) when the algorithm is per-pixel or global-stats).
2. **Operator class** (`src/operators/rs/`) — a thin `RSOperator` subclass
   (name / displayName / group / description / memoryPolicy). Override
   `memoryPolicy()` honestly: `Streaming`, `MultiPassStreaming`, `FullRaster`,
   `ExternalProcess`, `UnsupportedForLargeRaster`.
3. **Schema** (`schema()`) — declare inputs with
   `schema::makeRasterParam/makeIntegerParam/makeEnumParam/...` and **real
   outputs** via the `outputs` object: file outputs
   (`makeOutputParam("output", "...", "tif"|"csv"|"shp"|...)`) and auxiliary
   numeric/string outputs. The descriptor parses these — never fake a single
   Raster output for a statistics-only operator.
4. **Structured outputs** — `run()` returns an object with `output` (path)
   plus any numeric/string auxiliaries (mean, counts, ...). These appear in
   the descriptor `outputs` and in provenance.
5. **Metadata** (`metadata()`) — purpose, tags, prerequisites, workflowHints,
   limitations; plus planning hints: `largeRasterSafe` (derived from
   memoryPolicy automatically), `supportsCancellation`, `deterministic`,
   `costClass`, `facadeOf` (comma-separated primitive ids when this operator
   is a facade).
6. **Memory policy + estimates** — `executionEstimate()` for the typical
   input; override `estimateExecution(params)` for an input-dependent,
   overflow-safe estimate using `sicnu::processing::checkedMulN` /
   `makeStreamingEstimate` when the working set scales with the input
   (tileWidth × tileHeight × bands × bytes × simultaneous buffers + bands²).
7. **Preflight** — nothing to implement: preflight is generic over
   schema + ports + metadata. Declare `x-rs-contract` on ports to enable
   band-count / same-grid / radiometric-state checks:
   `param["x-rs-contract"] = ...` (`bands.min`, `gridRelation: "same-grid"`,
   `radiometricState: [...]`).
8. **Tests** (`tests/test_*.cpp`, registered via `sicnu_add_test`) —
   golden/equivalence (streaming == full-scene math), schema/descriptor
   (real outputs), validation, cancellation mid-run, edge tiles / NoData /
   NaN, and composition (output of A validates as input of B).
9. **Registration** — one `REGISTER_RS_OPERATOR(Class, "rs:id")` line in
   `src/operators/rs/rs_operators_init.cpp` + the source file in
   `src/operators/CMakeLists.txt`. The atomic registry mirrors the operator
   registry automatically. **Never create a third registry.**

## Rules of thumb

- One operator = one meaningful, independently callable processing step.
- Legacy selectors stay as facades (mark `facadeOf`); primitives share the
  kernel — do not copy algorithm implementations.
- Every long-running operator must poll `context.throwIfCancelled()` per tile
  so the TaskCenter / workflow / MCP cancel contract holds.
- Numeric correctness beats micro-optimization; when optimizing, benchmark
  before/after and keep a golden/equivalence test.
