# Runtime Architecture

The model runtime is the seam between a model manifest and every surface that
executes models. One path, no copies:

```text
models/<id>/model.json
      │  discovery (file scan) + registry (register/inspect/health)
      ▼
ModelCatalog  (src/operators/framework/model_catalog.*)
      │  stable id · version · content digest · readiness · contracts
      ▼
ModelRuntimeRegistry  = bounded ModelSessionPool (src/operators/runtime/model_runtime.*)
      │  key: framework | device | sha256(artifact bytes)
      │  LRU bound · idle eviction · per-key release · PoolStats
      ▼
IModelRuntime session (load / warmup / infer / cancel / health / memory / shutdown)
      │  backends: opencv_dnn (built-in) · onnxruntime (SICNU_WITH_ONNX_RUNTIME)
      │            · plugin providers (registerProvider seam)
      ▼
runModelInference(request, context)   (src/operators/runtime/model_execution_service.*)
      │  resolve → gates → preflight → acquire → engine → result payload
      ▼
TileInferenceEngine (raster stack)   DetectionTileEngine (boxes → vector)
      ▼
atomic publication: same-dir `.tmp~` stage + rename (raster) / vector sidecars
      ▼
rs:infer · rs:segment · rs:detect · rs:embedding   (thin JSON adapters)
      ▼
CLI · Workflow/TaskCenter · MCP/Pi · GUI task helpers   (via Operator Registry)
```

## Session identity

A session's cache key is `framework | device | sha256(artifact bytes)`.
Replacing the bytes under a weight path therefore produces a new identity:
the old session is only reachable through live `shared_ptr`s until it leaves
via LRU/idle eviction, and every new acquire loads the new bytes. Bytes that
are equal at different paths share one session — weights dominate memory.
Ad-hoc (non-catalog) models get their digest computed at acquire, memoized by
(path, size, mtime); unreadable artifacts fall back to path/size/mtime
identity (no real provider can load unreadable bytes, so no stale-content
session is reachable).

## Backend contract

`IModelRuntime` (one loaded session, serialized forward passes):

| Operation | Semantics |
|---|---|
| `infer` / `infer(name)` / `inferMulti` | one forward pass; detached output Mat |
| `warmup()` | throwaway probe forward; never fatal, outcome lands in `health()` |
| `requestCancel()` / `clearCancel()` | cooperative; the NEXT checkpoint throws. A forward already handed to the backend runs to completion (honest limitation) |
| `health()` | ok / forwards / failures / lastForwardMs / lastError |
| `memoryEstimate()` | weightsMb (from disk) + workingSetMb (0 = unknown) |
| destruction / eviction | frees the session; GPU state is released with the process or on `release()` / `releaseAll()` |

Per-backend coverage: [backend-compatibility.md](backend-compatibility.md).

## Execution seam

`runModelInference` (`src/operators/runtime/model_execution_service.*`) is the
only execution path. It performs, in order:

1. input existence (FileNotFound),
2. model resolution (`resolveModelReference`: file path or catalog id; the
   catalog misses trigger exactly one reload),
3. catalog readiness gate (readiness → `FileNotFound`/`InvalidInputData`),
4. runtime verdict (provider present, device feasible → `InvalidInputData`),
5. loud refusal of unwired contracts (temporal length, multi-input),
6. feature-cube band-role preflight (when the input declares a cube),
7. device parse (`InvalidParameter` on garbage),
8. session acquire (device-aware cache),
9. engine run (raster or detection), atomic publish inside the engine,
10. compatibility payload (`output/backend/device/model/outBands/width/height/
    tileSize/tiles/tilesSkippedNoData` — plus detection/embedding extras).

## Registry

`ModelCatalog` is authoritative: file scanning is the discovery channel;
`registerManifestJson` adds session-scoped entries that shadow scanned
manifests; `unregister` hides entries until the next `reload()`;
`validateManifestJson` is pure (registers nothing); `resolve("id@version")`
and `health(id)` serve agent surfaces. See
[model-manifest.md](../models/model-manifest.md).

## What is intentionally NOT here

- No LLM/chat runtime — `src/runtime/**` (execution plane) and Pi's agent
  loop are separate subsystems; this layer executes raster models only.
- No second inference path anywhere: classical-ML classification is its own
  ADR 0019 deep module; provider algorithms (GDAL/OTB/QGIS) never enter this
  layer.
- No remote (HTTP/Python-process) raster inference runtime in 4.0 — external
  providers integrate through `registerProvider` (the plugin bridge does),
  adopting the same cancel/health semantics.
