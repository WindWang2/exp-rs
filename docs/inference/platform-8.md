# Model Runtime Platform 8.0 — multimodal, temporal, provenance

Platform 8.0 deepens the 7.0 unified runtime (ADR 0142) along six axes. It
introduces NO second runtime, catalog, scheduler or wire protocol — every
addition rides the authoritative seams. Code lives in
`src/operators/runtime/**` (engine, planner, providers) and
`src/operators/framework/model_catalog.*` (manifest contracts).

## 1. Grid authority & provenance (WP-C)

Multimodal feeds are checked for co-registration as a GEODETIC verdict, not
just numeric agreement:

- size + geotransform within 1e-6 (7.0 behavior, unchanged);
- **CRS equality** — identical geotransform numbers under different CRS
  describe different ground and are refused. Comparison is semantic
  (GDAL `OGRSpatialReference::IsSame`), so WKT and `EPSG:XXXX` spellings of
  the same CRS agree. Temporal frames and quality masks are CRS-checked too.
- `inputs[].alignment: "reference"` strictens the verdict: a feed with NO
  CRS is then a refusal (an unverifiable feed is a misaligned feed).

The runtime still NEVER warps. Pre-alignment is an explicit caller step
through the geospatial seam (`raster_convert` warp); the feed entry carries
`prepared_from` (per-frame original paths) which the engine verifies for
arity and records verbatim.

Every successful raster product publishes `<output>.prov.json`
(`exp-rs-prov/1`) atomically next to the raster: model identity + content
digest, backend/device, per-input grid provenance (path, prepared_from, CRS,
`crs_verified`), execution counters and output band semantics. A product
whose sidecar cannot be published is REMOVED — no untracked results. The
result payload carries the same story under `inputs`.

## 2. Temporal tensor lane (WP-D)

Beyond the 7.0 channel fold (`temporal_collapse: "channels"` →
`N,(T·C),H,W`), inputs may declare an explicit time axis:

- `temporal_collapse: "sequence"` + `layout: "NCTHW"` → rank-5 tensors
  `(N,T,C,H,W)` through `inferNamed`;
- `temporal_dynamic: true` → the FEED defines T (≥ 2 frames required);
  `missing_timestep` never applies;
- `timestamps` per feed frame (ISO 8601) are validated strictly increasing —
  a misordered series is a typed refusal, never a silent sort;
- `quality_masks` per feed frame (single-band rasters, 0 = invalid) turn
  invalid pixels into NoData: excluded from the valid-coverage gate AND
  zeroed for the forward, halo included.

`rs:infer` accepts `named_inputs[]` feeds (name, paths, bands, timestamps,
quality_masks, prepared_from) — the operator surface that previously could
not reach the multi-input path at all — and expands a LOCAL STAC
Collection/Item document (`stac_collection` + optional `stac_asset`) into a
time-ordered frame list with timestamps. Network STAC stays with the
app-layer STAC client; the operator refuses remote documents instead of
silently fetching.

## 3. Device planner 2.0 (WP-B)

- `DevicePlacementPolicy` knob: `LowestFitting` (7.0 default) or
  `LeastLoaded` (auto picks the fitting device with the most free VRAM,
  ties → lowest index). Explicit `cpu`/`cuda:N` requests are
  policy-independent. It is a placement knob only — admission, the bounded
  pressure valve and refusal semantics are unchanged.
- `ModelRuntimeRegistry::deviceReport()` exposes the per-device
  capacity/reserved/holders snapshot. There is no sub-allocation, so
  `capacity − reserved` IS the honest fragmentation view.

## 4. Pre/post contract completion (WP-E)

- `postprocess.class_mapping: [p0, p1, ...]` remaps MODEL classes to
  PRODUCT classes in `labels` products (identity when absent). The mapping
  must be injective and non-negative (collisions would silently merge
  classes), must cover the head's class planes exactly, and the raster
  palette metadata carries product-class ids. Mask/confidence semantics
  stay on MODEL classes so the background test never moves under a remap.

## 5. Provider ecosystem (WP-F)

- Python worker handshake may declare `capabilities` (max_rank, multi_input,
  input_dtypes, output_dtypes); declared values REPLACE the provider
  defaults so consumers negotiate against what the worker actually supports.
- A worker that DIES mid-exchange gets ONE respawn + replay for the session
  lifetime (dead worker only — a live-but-stuck worker is a hang, never
  restarted, since replaying into it could double-execute). An exhausted
  budget or failed restart is a typed `ProviderCrash` diagnostic.
- The wire contract stays `exp-rs-infer/1` — the capability block is an
  additive, backward-compatible extension of the READY event, not a fork.

## 6. Real ONNX Runtime lane (WP-A)

The ORT provider (7.0 source, never before compiled) now builds against the
official ORT SDK layout (CMake accepts `include/onnxruntime_cxx_api.h` and
distro `include/onnxruntime/...`). When `SICNU_WITH_ONNX_RUNTIME=ON` and the
SDK is found, `test_onnxruntime_provider` executes the REAL lane: named
multi-input known answers, rank-3/4 multi-head selection, dynamic shapes on
one session, Int64/Double/UInt8 transport with cv-bridge refusals, warmup
probe accounting, cancel-before/in-forward cancellation (`RunOptions
SetTerminate`) with session reuse, health/memory honesty, and the CUDA-less
host refusal. Without the SDK the target is not built — capability gating,
never a fake pass.

## Acceptance evidence index

| Acceptance target | Evidence |
|---|---|
| Real backend lane runs named N-D inference | `test_onnxruntime_provider` (ORT 1.20.1, CPU EP) |
| No implicit warp on misaligned multimodal input | `crsMismatch` refusals + 7.0 grid checks; provenance records verdicts |
| Race-safe reservations within declared VRAM | 7.0 ledger tests + `test_device_planner` (unchanged, green) + WP-B tests |
| Provider crash/timeout/cancel → typed diagnostics | `test_provider_python` (restart budget exhaustion) |
| Temporal with explicit time/missing semantics | `test_model_runtime_8` sequence/dynamic/timestamps/quality tests |
| Confidence/uncertainty/provenance truthful + governed | `.prov.json` sidecar tests; uncertainty paths unchanged |
