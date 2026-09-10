# ADR 0142: Model Runtime & Multimodal Inference Platform 7.0

- Status: Accepted (Model Runtime 7.0 goal series)
- Context: Platform 4.0 (ADR 0130) delivered the unified execution seam, the
  content-identity session pool, the cv::dnn provider contract, detection
  decoding and the OOM ladder. The 7.0 goal asked the runtime to serve modern
  remote-sensing deep learning: multi-input (optical/SAR/DEM/mask/aux),
  multi-temporal (T×C×H×W), multi-output typed-head models, deterministic
  multi-GPU placement, out-of-process providers, a completed failure taxonomy
  and an evidence matrix. Audit findings pinned the real gaps: `inferMulti`
  existed only at the provider boundary (no tiled execution path fed it),
  inputs bound by POSITION instead of name, the transport was hard-locked to
  rank-4 float32, `auto` device resolution was a trivial cuda:0 with no
  cross-task VRAM accounting, four failure kinds were missing, and unknown
  manifest keys were silently ignored (the #646 failure class).
- Decision:
  1. **Manifest 7.0 (closed vocabulary)**: every declared manifest section is
     a CLOSED set — unknown keys are a typed `InvalidManifest` refusal, never
     silently ignored. Documented annotation keys (`note`, `reference`,
     `x-*` vendor extensions) and derived output projections (readiness,
     digests, mirrors) stay legal so authoring and re-registering inspected
     manifests keep working; 26 shipped manifests verified zero-violation.
     New contract surface: per-input `modality`
     (optical/sar/dem/mask/aux), `alignment` (reference co-registration
     requirement), `missing_timestep` (refuse|zero); `output.heads[]` typed
     heads (segmentation/classification/detection/embedding/uncertainty/
     auxiliary with layout/dtype/class-schema/confidence semantics; the flat
     fields remain the heads[0] mirror); `preprocess.clamp_min/clamp_max/pad`;
     `tiling.min_valid_coverage`; `runtime.provider.{url,worker_script,
     interpreter,timeout_ms,max_body_mb}`; `manifest_version` 5.
  2. **N-D named transport**: `TensorBlob` is the runtime-side owning tensor
     (rank 1..6, exact dtypes; Int64/Float16 refuse the cv bridge instead of
     bit-casting). `IModelRuntime::inferNamed` is THE multi-input entry point
     with a default bridge over the historical cv::Mat surface (typed rank
     refusal), and `capabilities()` declares what a session actually supports
     (multiInput/namedBind/maxRank/cancelInForward/dtypes) so consumers
     negotiate instead of assuming. The ORT provider binds by NAME (manifest
     input names must match graph inputs), carries true N-D tensors and
     honors the resolved CUDA index; cv::dnn binds by name through setInput.
  3. **Deterministic device planner** (placement seam, NOT a scheduler):
     `DeviceInventory` (injectable, env-overridable — multi-device decisions
     are testable on single-GPU hosts) plus a per-device `VramLedger`.
     GPU acquisitions reserve their manifest estimate for the cache-entry
     lifetime; `auto` picks the LOWEST FITTING device instead of trivially
     cuda:0; explicit cuda:N must fit its own card. A memory-pressure valve
     evicts cached sessions (ONE bounded pass — explicit cuda:N evicts only
     that device) and retries resolution/admission once; exhaustion is a
     typed DeviceUnavailable refusal. Reservations are released on every
     eviction/release path; concurrent acquire/release respects the bound.
  4. **Multimodal/temporal tiled execution**: `runMultiInput` tiles several
     named raster feeds on ONE common grid (primary feed is the grid
     authority; co-registration enforced by size + geotransform within 1e-6 —
     misaligned feeds are a typed refusal pointing at the geospatial
     alignment seam, never an implicit warp). Temporal feeds fold frames into
     the channel axis (`channels` collapse); missing frames follow the
     contract policy (refuse | explicit zero). Validity is computed on RAW
     windows BEFORE the NaN→0 zeroing (post-preprocess zeros would mask
     NoData); `min_valid_coverage` widens the skip set without touching the
     OOM ladder semantics. `runModelInference` routes named feeds; the 4.0
     "not wired" refusal now fires only when a multi-input/temporal model is
     run WITHOUT the feeds it demands.
  5. **External provider contracts**: one shared `exp-rs-infer/1` wire
     document (JSON + base64 tensors). The HTTP provider (Qt6::Network,
     graceful-degradation stub without it) and the Python worker provider
     (out-of-process interpreter, newline-delimited JSON, ready handshake)
     register through `ModelRuntimeRegistry::registerProvider` — no second
     catalog, no second runtime. Provider registration takes the registry BY
     REFERENCE: calling `instance()` inside the registry constructor
     re-enters the static initializer (a latent Platform 4.0 bug the ORT
     stub path had masked).
  6. **Failure taxonomy completion**: `InferenceFailureKind` gains
     IncompatibleSchema, DeviceUnavailable, ProviderCrash, OutputInvalid with
     a stable `errorCodeForInferenceFailure` projection (+`DeviceUnavailable`
     3006, +`RuntimeProviderFailed` 3007, append-only). Classification order
     keeps every 4.0 pin green.
- Consequences:
  - Adding a contract-conforming modern model = manifest + weights/provider;
    no C++ inference framework is copied per model.
  - The engines keep their cv::Mat fast paths; TensorBlob materializes at
    provider boundaries only (measured: multi-input + T=3 temporal runs cost
    about the same per tile as single-input at 64 px tiles in the local
    benchmark).
  - ORT-provider code paths compile only under SICNU_WITH_ONNX_RUNTIME; the
    locally unavailable dependency keeps them behind the same
    graceful-degradation contract, with the decision logic exercised through
    the default bridge and fake providers.
- Non-goals: automatic warp-to-grid reprojection (declaration + refusal
  only — reprojection stays a geospatial seam), multidim/SOI data sources
  (file-list temporal first), distributed/multi-host execution, training.
