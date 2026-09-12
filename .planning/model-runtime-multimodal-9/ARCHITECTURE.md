# ARCHITECTURE — Model Runtime & Multimodal EO Inference 9.0

Master stays on the 8.0 seams (ADR 0142/0143). 9.0 deepens the SAME seams —
no second runtime, catalog, scheduler, or I/O authority. This file records
the DESIGN DECISIONS per milestone; implementation details live in the code.

## D1 — Real device truth (M1/M2)

Today `ModelHardwareCapabilities::detect()` is env-var driven; on a real GPU
host that is a lie of omission. 9.0:

- `detect()` gains an **NVML backend** (dlopen `libnvidia-ml.so` at runtime,
  no link-time hard dependency): real device count, name, total/free VRAM,
  compute capability per device. NVML failure → fall back to the historical
  detection (env + no CUDA) and record why.
- `SICNU_MODEL_*` env overrides REMAIN the test seams and win over NVML —
  determinism for tests, reality for production.
- New `SICNU_MODEL_VRAM_FREE_MB` (csv per index) test override for free VRAM.
- `DeviceInventory` carries per-device free VRAM; the registry refreshes the
  live inventory at acquire-decision time and feeds REAL free VRAM into the
  existing `resolveDevice(..., freeVramMbByIndex, ...)` seam. The ledger
  remains the reservation authority; real free VRAM bounds admission.
- CUDA EP (onnxruntime provider): when the requested device is CUDA, the
  provider ATTEMPTS `SessionOptionsAppendExecutionProvider_CUDA(deviceId)`.
  Failure (provider lib absent, no driver, OOM at init) → typed
  `DeviceUnavailable` refusal — never a silent CPU demotion unless the
  manifest allows fallback (existing `allowCpuFallback` semantics).
  Success is verified by an execution-provider probe recorded in the payload
  (`provider_details.ep`), not assumed from absence of error.

Rationale: 8.0 could only refuse CUDA honestly (CPU-only SDK). This host HAS
a real RTX 3080 + a working CUDA stack (ORT 1.30 GPU + cuDNN via pip,
verified: Conv on CUDA). Real execution evidence now replaces refusal-only
evidence; the refusal path stays tested via capability gates.

## D2 — Provider matrix completion (M1)

- Error taxonomy extends with CUDA-shaped failures mapped into the EXISTING
  kinds (DeviceUnavailable, OutOfMemory, ProviderCrash) — no new kinds
  unless a failure cannot be classified honestly.
- Python worker: capability handshake may declare `gpu: true`; the worker's
  ORT provider selection honors the requested device token; timeouts get a
  deterministic kind (Timeout → Canceled family, message-tagged) instead of
  a raw kill message.
- The C++ ORT provider compiles against the shipped 1.20.1 layout headers
  (C-API is versioned; linking a newer runtime is supported) — CMake
  discovery gains the GPU-SDK layout as a hint, never a requirement.

## D3 — Per-feed preprocessing contract (M3)

Manifest `inputs[]` entries gain additive optional `preprocess` override
(closed vocabulary: normalize none|linear|mean_std, scale, mean[], std[],
applied AFTER the global contract is resolved but BEFORE band roles are
checked — wait: order is: effective preprocess = input.preprocess if present
else model.preprocess). Validation: mean/std arity == fed channel count;
closed normalize vocab; unknown keys refused. The engine resolves per feed;
the provenance sidecar records the EFFECTIVE preprocess per feed.
Band mapping stays `feed.bands` (1-based) — no second mechanism.

## D4 — Input fingerprint (M3)

`feedFingerprint(path, bands)` → deterministic JSON:
`{width, height, bandCount, bandDtypes[], geotransform[6] (bit-exact), crs,
selectedBands[], content: {sha256, bytes} when file ≤ contentDigestMaxBytes
(default 256 MiB, run option) else {bytes, mtime, reason:"file-too-large"}}`.
Honest: the structural part is always present; the content digest is marked
as such, never implied. Sidecar + payload carry it per feed.

## D5 — Temporal chunking (M4)

Additive manifest input token `temporal_chunk: N` (≥1) valid ONLY with
`temporal_independence: true` (model output for a frame never depends on
frames outside its chunk). The sequence/collapse assembly then feeds windows
of ≤N frames per forward; per-frame outputs are reassembled in time order.
Memory becomes O(chunk·C·H·W) instead of O(T·C·H·W) — the "no whole-series
unbounded memory" requirement at T=1024. Without independence declaration,
chunking is a typed refusal (science never silently changes). Per-frame
provenance records (frameIndex → chunkIndex, forwardIndex).

## D6 — Feather blending (M5)

Additive manifest `tiling.blend: "none" (default, historical hard edge) |
"feather"`. Feather: per-tile weight w = 1 in the core, cosine ramp over
`min(halo, blend_width)` px into the halo. Class-probability planes are
accumulated `Σ w·p` / `Σ w` in a bounded row-window accumulator (tiles are
planned row-major; a row is final once tiles `blend_rows` below it have run
— memory O(tileH+halo) × W × C, never O(W×H×C)). Derived output modes
(labels/mask/confidence) collapse AFTER blending — blending probabilities
then argmax, never blending labels. Provenance records blend method+width.
Detection vectors are NOT blended (NMS owns overlap); embedding runs are
not tiled products.

## D7 — Embedding product semantics (M5)

`rs:embedding` publishes a defined product: a single-band float32
georeferenced raster when the embedding is per-pixel (C→1 principal band is
NOT invented — instead: embedding length 1 writes the band; length >1
writes a multi-band float32 raster with band semantics
`embedding_dim<k>`), plus sidecar fields `embedding {dims, pooling,
model}`. No silent PCA/scalarization — the raw embedding IS the product.

## D8 — Per-class product metadata (M6)

Labels/mask products gain per-class metadata from the model contract:
class names (M7 ontology), per-class pixel counts (computed during the
final streaming pass, O(1) memory), stored in payload + sidecar
(`classes: [{id, name, pixels}]`). Threshold/NoData classes are explicit
entries or excluded-with-reason — never unnamed silently.

## D9 — Package identity (M7)

Manifest additive `package.aux_files[]: {path (model-dir-relative),
role, sha256}`. ModelCatalog resolve: verifies each digest (streaming);
mismatch → readiness failure typed `IntegrityError`. Session identity
extends to `sha256(weights digest ‖ sorted(role,digest)...)` ONLY when
aux_files exist (old manifests: identical identity math — no migration).
Duplicate stable-id with DIFFERENT weights digest across the catalog scan
→ typed conflict at scan (both versions named), not a silent shadow.

## D10 — Consumer-side provenance verification (M8)

New `provenance_verify.h` (runtime ownership):
`verifyProductProvenance(outputPath, expectation?) → ProvenanceVerdict`
with typed states: Ok | MissingSidecar | MalformedSidecar | ModelMismatch |
GridMismatch | StaleProduct | UnsupportedVersion. Checks: sidecar presence
(closes the 8.0 crash-window gap), schema/version parse, model id+digest
vs expectation (default: any well-formed), recorded output identity
(size/mtime/digest-when-small) vs the actual file (staleness detection),
required grid fields. The Experiment/MLOps seam consumes THIS function —
verification is one call, not a re-implementation.
Sidecar also gains execution identity: backend version strings
(ORT `GetVersionString()`, cv version, worker-reported version).

## D11 — Performance evidence (M9)

`test_model_runtime_bench` extended: CPU vs CUDA known-answer forwards,
cold/warm acquire, tile latency, cancel latency, concurrent session
throughput, ledger pressure, 100k-extent PLANNED tile counts (logical,
no giant rasters), model cache eviction. All numbers recorded with
environment (GPU model, driver, ORT version, build type) in PERFORMANCE.md.
Debug-vs-Release comparisons are forbidden; Release only.

## Compatibility contract

All manifest additions are optional + additive; every historical manifest
runs bit-identically (blend=none, no chunks, no aux_files, global
preprocess). Payload keys are additive only. Error codes extend the
existing projection, never renumber.
