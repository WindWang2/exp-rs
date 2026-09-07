# Device and Memory Policy

## Devices

A model runs on a **resolved device**: `cpu` or `cuda:<index>`. The request
comes from the manifest `runtime.device` token or an operator override
(`rs:infer`/`rs:segment`/`rs:detect`/`rs:embedding` `device` parameter).

| Token | Semantics |
|---|---|
| `auto` (default) | The lowest addressable CUDA device that fits the VRAM budget when the model tolerates GPU; otherwise `cpu`. A pure function: the same host always resolves identically. |
| `cpu` | Always CPU. |
| `cuda:N` | Device N, when N is addressable (device count AND the backend's index capability) and the estimate fits. Non-fitting budgets demote to CPU only when `cpu_fallback` is enabled; otherwise the request fails loudly. |
| garbage | `InvalidParameter` / `InvalidManifest` — never a silent fallback. |

Determinism: `resolveDevice` is a pure function of (request, detected
capabilities, model preference, VRAM estimate, backend index cap). Device
selection is part of the session cache key, so a fallback switch can never
serve a stale-GPU session.

Honest constraints (also in backend-compatibility.md):

- The `opencv_dnn` backend addresses exactly one CUDA device (`cuda:0`);
  `cuda:1+` fails loudly rather than silently running on another card.
- Multi-GPU hosts declare `SICNU_MODEL_CUDA_DEVICES=N` for backends that can
  address more (the conditional ONNX Runtime provider).
- `SICNU_MODEL_GPU=0|1` and `SICNU_MODEL_VRAM_MB=N` force detection results
  for tests and constrained deployments.

## VRAM / RAM budgets

- `runtime.estimated_vram_mb` is the model's declaration; readiness checks it
  against the host budget when `cpu_fallback` is off, and the runtime demotes
  to CPU when the budget is exceeded and fallback is on.
- Batch sizing (`effectiveBatchSize`) clamps the manifest batch by the VRAM
  budget; CPU-side RAM admission is owned by TaskCenter's resource budget fed
  by the operators' `estimateExecution` (the #689 math: halo window vs fed
  tensor, artifact-size weight floor, declared output channels).
- `IModelRuntime::memoryEstimate()` reports weights (from the artifact file)
  honestly; `workingSetMb` stays 0 where a backend exposes no allocator
  introspection — the engine's O(batch × tile × bands) model is the
  documented working-set estimate.

## The OOM ladder

`classifyInferenceError` recognizes OOM (message patterns over cv::Exception,
`bad_alloc`, CUDA OOM strings). In the tile engines:

1. batch > 1 → the pending batch is retried tile-by-tile (semantics unchanged,
   `batchReductions` surfaced in stats/results);
2. batch = 1 → terminal failure carrying the diagnostic ("free memory or use a
   smaller model — the engine never alters spatial resolution or model
   semantics").

The engines never respond to memory pressure by shrinking tiles, changing
spatial resolution, reducing spectral inputs, or otherwise altering model
semantics (goal §3). Any such change would be a silent scientific contract
violation; the platform fails explicitly instead.

## Shutdown and unload

- `ModelRuntimeRegistry::releaseAll()` drops every cached session (running
  callers keep their `shared_ptr`s until they drop them).
- `release(framework, identity)` unloads one model identity.
- `setIdleEvictionMs(ms)` evicts sessions untouched for longer than the idle
  window on the next acquire/inspect (0 = off).
- Unload semantics: "no longer handed out" — never "invalidates live
  pointers". The pool stats (`poolStats()`) expose
  cached/max/loads/hits/misses/evictions for benchmarking and health.
- GPU state is released with the session destruction (OpenCV) or the process;
  the application-level shutdown hook is a documented follow-up (the CLI
  worker and tests call `releaseAll()` explicitly).
