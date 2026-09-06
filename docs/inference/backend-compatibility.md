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

- The matrix is a contract, not a claim: every "yes" is exercised by a test
  named above or in `TEST_MATRIX` (`.planning/model-runtime-ai-platform-4/`).
- A single forward pass is uninterruptible on every backend — cancellation
  lands at checkpoints (between tiles/batches and at forward entry). This is
  the documented cost of not requiring a specific ONNX Runtime build.
- Adding a backend: implement `IModelRuntime`, register a factory (+ traits)
  under a new `framework` id; manifests referencing that id become executable
  with no further code. Remote (HTTP/process) runtimes take the same path and
  must honor the cancel/health semantics they declare.
