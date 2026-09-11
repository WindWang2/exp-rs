# ADR 0144: Model Runtime & Multimodal EO Inference Platform 9.0

- Status: Accepted (Model Runtime 9.0 goal series)
- Context: Platform 8.0 (ADR 0143) delivered a real ORT lane, CRS-aware
  grid authority, the temporal tensor lane, provider negotiation and
  provenance sidecars — with four honest gaps: CUDA EP execution was never
  performed (CPU-only SDK, no GPU truth beyond env vars), consumers could
  not DETECT missing/stale provenance, multimodal feeds shared one global
  preprocessing contract, tiles stitched with hard edges, and package
  identity covered only the weight bytes. This host provides a real RTX
  3080 (16 GB, SM 8.6) with a working GPU ORT stack (ORT 1.30 GPU + cuDNN
  via user-local pip, CUDA 13.3 driver), which turns the CUDA question
  from "refuse honestly" into "execute honestly".
- Decision:
  1. **Real device truth**: NVML (dlopen, optional) enumerates devices,
     names, total AND free VRAM per card, refreshed per acquire. Two CUDA
     gates stay distinct — `cudaAvailable` (cv::dnn backend, opencv_dnn
     path) vs `cudaRuntimeAvailable` (real driver; the gate DIRECT-CUDA
     runtimes resolve against, keyed on provider traits'
     `maxAddressableCudaIndex > 0`). Admission free-VRAM is the honest
     minimum of ledger-reserved and driver-reported free. Env overrides
     remain the test seams and win over NVML.
  2. **CUDA EP executed for real**: a resolved CUDA device attempts the
     CUDA EP at load; failures are typed `DeviceUnavailable`, never a
     silent CPU demotion. Sessions expose `providerDetails()`
     (execution provider + runtime version) into payloads and provenance —
     honest at ORT's granularity (EP registration truth, not per-op
     placement claims). A live-but-unresponsive provider is a new
     append-only `Timeout` kind (`ExternalProcessTimeout`), distinct from
     `ProviderCrash`.
  3. **Per-feed preprocessing**: `inputs[].preprocess` overrides the global
     contract per feed (same closed vocabulary, arity validated against the
     feed's channels); the effective contract is recorded per feed in
     payload + sidecar. Feed identity fingerprints (structure + bounded
     content digest) record what was fed, honestly distinguishing digest
     from size+mtime when files exceed the bound.
  4. **Feather blending**: `tiling.blend: "feather"` averages overlapping
     windows (cosine ramp across the halo) instead of hard-stitching cores;
     blending the CLASS PROBABILITIES before the derived collapse (never
     blending labels); NaN predictions skip; memory bounded by a sliding
     (tile + 2·halo)-row window. Requires halo > 0 and grid-preserving
     heads; multi-input refuses feather in 9.0 (typed, not silent).
  5. **Package identity**: `package.aux_files[]` digest-binds auxiliary
     files at resolve time; the package digest extends the session cache
     key. `content_digest` keeps meaning "weights digest".
  6. **Consumer-side provenance**: `verifyProductProvenance()` returns
     typed verdicts (MissingSidecar / Malformed / UnsupportedSchema /
     ModelMismatch / GridMismatch / StaleProduct) — the 8.0 crash window
     becomes a detectable state for every consumer, not a writer-side
     promise.
  7. **Contract truth pinned**: every parameter the model operators parse
     must be declared in their schema (#872 class) — mechanical regression
     in `test_model_runtime_9`.
- Rejected alternatives: a second GPU inventory source (vendor SDKs) —
  NVML via dlopen is the driver contract and stays optional; per-op EP
  placement claims — ORT does not expose them and inventing them would be
  fabrication; blending labels/masks per tile — mathematically wrong
  (class collapse must follow the blend); temporal chunking — the 8.0
  sequence contract fixes raster outputs at rank-4, so there is no
  per-frame output to reassemble; chunking is deferred until a per-frame
  product contract exists.

Detailed user-facing documentation: `docs/inference/platform-9.md`.
