# OWNERSHIP — seams this track touches vs leaves alone

## Authoritative seams (never fork)

| Seam | Owner | This track's relationship |
|---|---|---|
| Agent loop | Pi (`pi/`) | none |
| Workflow execution | `WorkflowRunCoordinator -> TaskCenter -> JobEngine -> Executor` | add thin trace/fault probes AT the seams |
| Operator surface | `RSOperatorRegistry` | fuzz its schema contracts read-only |
| Model execution | `runModelInference` / `IModelRuntime` | fault probe at provider boundary; benchmark tiling via existing operators |
| Rendering | QGIS | none (GUI out of local scope) |
| Persistence | `DatasetStore` / `ExperimentStore` | fault probes at transaction boundaries; fuzz manifests |
| Geospatial I/O | `src/geospatial/**` | consolidate first-party GDAL compat there |
| Publication | `OutputCommitter` / `ArtifactObjectPool` | extend existing fault points; add trace |
| Command/UX state | `CommandRegistry` / `ContextRules` | none |
| Observability | `src/runtime/observability/**` | extend (adapters, sinks) — single source of trace/fault truth |

## Vendored code (DO NOT EDIT)

- `src/core/**`, `src/gui/**` QGIS-derived files (qgs*.cpp/h): upstream
  vendored. Their scattered `GDAL_VERSION_NUM`/`_WIN32` checks stay as-is;
  the compat header targets **first-party** code only
  (`src/geospatial/**`, `src/processing/**`, `src/runtime/**`,
  `src/data/**`, `src/sdk/**`, `src/workflow/**`, `src/dataset/**`,
  `src/experiment/**`).
- `vendor/`, `external/`, `otb_ref/`: untouched.

## Cross-track exposure (minimize shared-file edits)

- `tests/CMakeLists.txt`: additive blocks at the verification section only.
- `src/app/main.cpp`: already hosts the trace bootstrap; if touched, one
  additive include + call at most.
- `src/processing/framework/**`: trace/fault probes are 5-15-line insertions
  at existing broadcast points (signals / telemetry calls); no control-flow
  changes.
- Dataset/experiment stores: probes inside existing transaction functions,
  one branch each.
- No edits to `src/core/**`, `src/gui/**` product logic.

## Files this track expects to own (create)

- `src/geospatial/util/gdal_compat.h` (first-party GDAL version seam)
- `scripts/verification_ladder.py` (layered runner)
- `scripts/collect_readiness.py` (release-readiness report)
- `tests/test_known_answer_corpus_8.cpp`, `tests/test_contract_fuzz_ops.cpp`,
  `tests/test_contract_fuzz_ipc.cpp`, `tests/test_fault_matrix_8.cpp`,
  `tests/test_trace_chain_8.cpp`, `tests/test_portability_contract.cpp`,
  `tests/benchmark_scale8.cpp`
- `docs/verification/PLATFORM_MATRIX.md`, `docs/verification/READINESS.md`
  (schema + generated example)
- `.planning/verification-platform-8/**` (track notes)
