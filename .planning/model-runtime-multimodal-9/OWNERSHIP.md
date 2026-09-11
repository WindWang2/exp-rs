# OWNERSHIP — model-runtime-multimodal-9

Inherited from 7.0/8.0 (ADR 0142/0143), re-verified on `origin/master`
`132da5e998`. This track owns the model runtime; it never duplicates
neighboring authorities.

## Authoritative seams this track MUST reuse

| Concern | Authoritative seam | Rule |
|---|---|---|
| Model execution | `runModelInference` (`src/operators/runtime/model_execution_service.*`) | Only path from operators to engines |
| Session lifecycle | `ModelRuntimeRegistry` (`model_runtime.*`) | Providers via `registerProvider`; no second pool |
| Operator surface | `RSOperatorRegistry` (`rs:infer/rs:segment/rs:detect/rs:embedding` in `src/operators/rs/`) | New params ride existing schemas; runtime truth lives HERE |
| Tensor transport | `TensorBlob` / `NamedTensor` (`tensor_blob.h`) | No new tensor type |
| Device placement | `DeviceInventory` + `VramLedger` + `resolveDevice` + placement policy (`device_planner.*`, `model_runtime.h`) | Placement/admission only; never a scheduler |
| Manifest contracts | `ModelCatalog` + `ModelInfo` (`src/operators/framework/model_catalog.*`) | Additive closed vocabularies + migrations |
| External provider wire | `exp-rs-infer/1` (`provider_wire.*`) | Version-negotiate, never fork |
| Raster reads/grid | `src/geospatial/raster` (`RasterReader`) | No inline GDALOpen beyond existing usage |
| Explicit warp/align | `src/geospatial/convert/raster_convert.h` | Caller-requested + provenance-tracked; never implicit |
| Atomic publication | engine staging/backup/rename + `geospatial/util` publish | Sidecars publish the same way |
| Out-of-process | `ExternalProcess` (SDK) | Python worker keeps using it |
| Observability | `src/runtime/observability` | Model diagnostics stay in runtime payloads |
| Agent capability knowledge | `src/agent/harness/**` + drift tests | Change knowledge + drift tests together |
| Experiment recording | ExperimentStore (data track) | Model runtime exposes evidence; never writes experiment history |

## Directories this track OWNS

- `src/operators/runtime/**`
- `src/operators/framework/model_catalog.*` (additive)
- `src/operators/rs/rs_inference_operator.cpp` and sibling model operators
  (`rs_segment/rs_detect/rs_embedding`) — model semantics only
- `tests/test_model_*`, `tests/test_onnxruntime_provider`,
  `tests/test_multimodal_inference`, `tests/test_tensor_blob`,
  `tests/test_provider_http`, `tests/test_provider_python`,
  `tests/test_device_planner`, `tests/data/*infer*`, `tests/data/*model*`
  fixtures for model suites
- `docs/inference/**`, `docs/models/model-manifest.md`,
  `models/*.json` (shipped manifests, additive),
  `models/README.md`
- `src/python/*worker/infer*` python worker code for the model provider
  (verify actual path at implementation; ownership limited to the model
  worker entry, not the shared worker harness)

## Shared files — minimal, end-of-milestone edits only

- root `CMakeLists.txt` (ORT discovery hints — GPU SDK layout)
- `src/operators/CMakeLists.txt`
- `tests/CMakeLists.txt` (new suites)
- `CHANGELOG.md`, ADR file (one 9.0 ADR)
- `.gitignore` (planning-dir exception)
- `src/agent/harness` capability knowledge (additive vocabulary only,
  with drift tests)

## Explicitly NOT owned

Agent planning, ExperimentStore, WorkflowRunCoordinator/TaskCenter/JobEngine
(the scheduler seam), generic geospatial I/O, GUI/workbench, plugin host.
Scheduler coordinates THROUGH the resource seam (`deviceReport`,
ledger admission); it never re-implements placement.
