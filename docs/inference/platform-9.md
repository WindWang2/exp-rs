# Model Runtime Platform 9.0 — real devices, per-feed truth, blending, package identity

Platform 9.0 deepens the 8.0 seams (ADR 0142/0143). No second runtime,
catalog, scheduler or wire protocol exists — every addition rides the
authoritative seams. Code: `src/operators/runtime/**`,
`src/operators/framework/model_catalog.*`.

## 1. Real device truth (NVML) — M1/M2

`ModelHardwareCapabilities::detect()` now probes the REAL NVIDIA driver
through NVML (`nvml_inventory.*`, dlopen — no link-time dependency):
device count, product names, total AND free VRAM per card, refreshed on
every acquire so admission decisions see live pressure. Two CUDA gates
exist, by design:

- `cudaAvailable` — the cv::dnn CUDA backend (opencv_dnn runtime path).
- `cudaRuntimeAvailable` — a real driver + device exists; DIRECT-CUDA
  runtimes (onnxruntime, the only provider whose traits address multiple
  devices) resolve against this gate. A host with a real GPU and an
  OpenCV-without-CUDA build now runs ONNX models on the GPU; an
  OpenCV-CUDA build without a driver still refuses honestly.

Free VRAM admission = min(ledger-reserved free, driver-reported free):
other processes on the card count. Capacity = min(driver total, env
budget). The `SICNU_MODEL_*` env overrides remain the test seams
(`SICNU_MODEL_GPU`, `SICNU_MODEL_VRAM_MB`, `SICNU_MODEL_CUDA_DEVICES`,
new `SICNU_MODEL_VRAM_FREE_MB` csv, `SICNU_MODEL_NO_NVML=1`) and win over
NVML — tests stay deterministic on every host.

The CUDA execution provider is attempted for real when a CUDA device is
resolved; failures (provider library absent, driver unusable, cuDNN
missing) are typed `DeviceUnavailable` — never a silent CPU demotion.
Sessions expose `providerDetails()` (`execution_provider`,
`runtime_version`) surfaced in result payloads (`provider`) and the
provenance sidecar (`execution.execution_provider` / `runtime_version`).

Taxonomy completion (append-only): a LIVE-but-unresponsive provider
(`timed out`) is now `Timeout` → `ExternalProcessTimeout`, distinct from
`ProviderCrash`; CUDA-stack failures map to `DeviceUnavailable`.

Python worker: the session pins `CUDA_VISIBLE_DEVICES` to the resolved
device before the worker starts, and the handshake `capabilities` block
may declare `providers` / `runtime_version` (surfaced verbatim).

## 2. Per-feed preprocessing + feed fingerprints — M3

`inputs[].preprocess` (manifest, closed vocabulary, additive) overrides
the global preprocess PER FEED: optical mean_std vs SAR linear vs DEM
none in one multimodal graph. Arity of mean/std is validated against that
feed's fed channel count. The EFFECTIVE contract is recorded per feed in
payload + sidecar (`inputs[].preprocess`). Feeds without the override run
the global contract bit-identically.

`TileInferenceEngine::feedFingerprint(path, bands, maxBytes)` records a
deterministic identity document per feed: width/height/band dtypes/
geotransform/CRS/selected bands always; a real SHA-256 content digest
when the file fits `fingerprint_content_max_bytes` (default 256 MiB),
else an honest `{bytes, mtime_utc, reason: "file-too-large"}` — the
fingerprint states exactly what it verified.

## 3. Feather tile blending — M5

`tiling.blend: "feather"` (manifest) or the rs:infer `blend` parameter
averages overlapping tile windows instead of hard-stitching cropped
cores: weight 1 inside the core, cosine ramp across the halo
(`0.5·(1−cos(π·d/halo))`), final pixel = Σ(w·v)/Σw. NaN predictions are
skipped (weight 0); pixels no tile predicted stay NoData. Requirements
(typed refusals otherwise): halo > 0 and grid-preserving head geometry;
`resize_to_input` heads and the multi-input engine refuse feather in 9.0.
Derived output modes (labels/mask/confidence) blend the CLASS
PROBABILITIES first and collapse afterwards — never the reverse. The
uncertainty band blends like any other band. Memory stays bounded: a
sliding row window of (tile + 2·halo) rows, never the whole raster.

## 4. Package identity — M7

`package.aux_files[]` (path, role, checksum, size_bytes) digest-binds the
files that travel with the weights (class ontology, preprocess config).
The catalog verifies every entry at resolve time — a missing, resized or
checksum-mismatched aux file is a typed readiness failure; the package
never half-loads. The `package_digest` (sha256 over the weights digest
plus every role:digest pair) extends the session cache key, so changed
aux bytes never reuse a session loaded for the previous package.
`content_digest` keeps its meaning: the weights' digest.

## 5. Consumer-side provenance verification — M8

`provenance_verify.h` turns the 8.0 write-side guarantee into a
consumer-side check: `verifyProductProvenance(outputPath, expectation)`
returns a typed verdict — `Ok`, `ProductMissing`, `MissingSidecar` (the
8.0 crash window, now detectable by the consumer), `MalformedSidecar`,
`UnsupportedSchema`, `ModelMismatch` (identity/digest/backend
expectation), `GridMismatch` (recorded geometry vs the actual raster) or
`StaleProduct` (product mtime newer than its sidecar — the rewritten
product). The Experiment/MLOps evidence seam consumes this function.

## 6. Contract truth — M0

`rs:infer` now declares `device` and `blend` in its schema (#872 class);
`test_model_runtime_9` pins the invariant "every parameter run() parses
is declared in schema()" for all model operators, so the drift class
cannot return silently.
