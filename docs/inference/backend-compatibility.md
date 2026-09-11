# Backend Compatibility Matrix

| Capability | opencv_dnn (`framework: "onnx"`) | onnxruntime (`framework: "onnxruntime"`) | plugin providers |
|---|---|---|---|
| Availability | built-in (with OpenCV) | compiled only with `SICNU_WITH_ONNX_RUNTIME=1` + dependency; otherwise a stub that reports `UnsupportedRuntime` | registered at runtime via `ModelRuntimeRegistry::registerProvider` (plugin bridge: `IPluginModelRuntimeV1`) |
| Devices | `cpu`, `cuda` (single device, index 0) | `cpu`, `cuda:0..N` (CUDA EP; provider compiled in) | provider-defined (`ProviderTraits.maxAddressableCudaIndex`) |
| `infer` (single NCHW) | yes | yes | provider-defined |
| named output head (`infer(name)`) | yes | yes | default = default head |
| multi-input (`inferMulti`) | yes | yes | `supportsMultiInput()` |
| `warmup()` | yes — probe forward, recorded, never fatal | yes | default no-op |
| `requestCancel()` | yes — checkpointed before each forward; a running forward is NOT interruptible | same default (provider may adopt ORT `SetCancel`) | default no-op |
| `health()` | forwards / failures / lastForwardMs / lastError | provider-specific | default empty |
| `memoryEstimate()` | weightsMb from disk; workingSetMb = 0 (no allocator introspection — honest) | provider-specific | default 0 |
| OOM ladder eligibility | yes (classified messages) | yes | yes — classify patterns are provider-visible |
| session cache | shared registry, key = framework\|device\|digest | same | same |
| tests | `tests/test_model_runtime.cpp` (identity ONNX fixture), failure matrix | compile-gated; not exercised in default CI | plugin bridge tests |

Notes:

- The matrix is a contract, not a claim: the built-in backend's
  warmup/health/memoryEstimate surface is exercised by
  `tests/test_model_failure_matrix.cpp` ("the runtime contract surface");
  provider-specific rows describe the provider's own responsibility.
- The opencv_dnn `auto` device selection prefers cuda:0 when it fits; picking
  a different index on multi-GPU hosts is a documented follow-up (explicit
  `cuda:N` requests work for index-capable providers).
- A single forward pass is uninterruptible on every backend — cancellation
  lands at checkpoints (between tiles/batches and at forward entry). This is
  the documented cost of not requiring a specific ONNX Runtime build.
- Adding a backend: implement `IModelRuntime`, register a factory (+ traits)
  under a new `framework` id; manifests referencing that id become executable
  with no further code. Remote (HTTP/process) runtimes take the same path and
  must honor the cancel/health semantics they declare.


## Platform 7.0: external provider contracts

Two out-of-process providers register through the SAME
`ModelRuntimeRegistry::registerProvider` seam — no second catalog, no second
runtime. Both speak one shared wire document (`exp-rs-infer/1`, JSON with
base64 tensor payloads, see `src/operators/runtime/provider_wire.h`).

| framework | transport | manifest keys | notes |
|---|---|---|---|
| `http` | POST JSON to `runtime.provider.url` (Qt6::Network; builds degrade to a stub → `runtime_unavailable`) | `url`, `timeout_ms`, `max_body_mb` | transport/HTTP failures map to `ProviderCrash`; a foreign `protocol` maps to `IncompatibleSchema` |
| `python` | out-of-process interpreter speaking newline-delimited JSON over stdin/stdout | `worker_script`, `interpreter` (default `python3`), `timeout_ms` | worker handshake: one `{"protocol":"exp-rs-infer/1","event":"ready"}` line; worker death maps to `ProviderCrash` |

### Capability negotiation

`IModelRuntime::capabilities()` declares what a session supports:
`multiInput`, `namedBind`, `maxRank`, `batch`, `cancelInForward`,
`inputDtypes`, `outputDtypes`. Consumers negotiate before feeding; a manifest
asking beyond the capabilities is a typed refusal, never silent
reinterpretation. Defaults describe the historical contract (single-input,
rank-4 float32, positional, coarse cancellation).

### In-forward cancellation

- `onnxruntime`: `requestCancel()` terminates a RUNNING forward via
  `Ort::RunOptions::SetTerminate` (`cancelInForward = true`).
- `onnx` (cv::dnn), `http`, `python`: cancellation is checked at forward
  boundaries only (documented limitation, `cancelInForward = false`).

### Failure taxonomy (7.0 completion)

`classifyInferenceError` kinds: OutOfMemory, Canceled, ShapeMismatch,
CorruptModel, NotLoaded, **IncompatibleSchema** (schema/contract/wire
mismatch), **DeviceUnavailable** (unaddressable or over-budget device),
**ProviderCrash** (worker death, connection lost, HTTP errors),
**OutputInvalid** (forward ran but output failed validation). The stable
error-code projection lives in `errorCodeForInferenceFailure`
(+`DeviceUnavailable` 3006, +`RuntimeProviderFailed` 3007).

## Platform 8.0 additions

- The onnxruntime column is EXECUTED for real when the build embeds the ORT
  SDK (both official `include/` and distro `include/onnxruntime/` layouts are
  discovered): named multi-input, rank-3..6 N-D transport, dynamic shapes,
  multi-head selection, in-forward cancellation (`RunOptions::SetTerminate`)
  and CUDA EP binding to the resolved `cuda:N` — the CUDA lane stays
  capability-gated on hosts without a GPU (typed refusal, never a claim).
- Python worker handshake may declare `capabilities` (max_rank, multi_input,
  input/output dtypes); declared values replace the defaults. A worker that
  dies mid-exchange gets ONE respawn + replay per session; exhaustion is a
  typed `ProviderCrash`.
- Device placement gained the `LeastLoaded` policy knob and a
  `deviceReport()` pressure snapshot — placement only, admission unchanged.
- See [platform-8](platform-8.md) for the full 8.0 surface.
