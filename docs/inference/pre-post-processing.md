# Pre/Post-processing Reference

All preprocessing and postprocessing is **declarative** (manifest contracts),
executed in exactly one place (the tile engines / the detection postprocess
module), and deterministic. Nothing is copied per model or per surface.

## Preprocessing (per tile window, in execution order)

| Step | Manifest | Semantics | Execution status |
|---|---|---|---|
| band select | `bands` operator param | 1-based band numbers; default all bands; arity must match `band_roles` | executed |
| band-role match | `input.band_roles` × feature-cube metadata | channel i = file band i; when the input declares a feature cube, its roles must cover the model's — mismatch fails the run | executed (preflight) |
| dtype check | `input.dtype` | must match the GDAL type of EVERY fed band | executed |
| NoData → NaN | band NoData values + `preprocess.nodata_policy: "zero"` | sentinels and non-finite samples become 0 for the model; a validity mask restores NoData after | executed |
| normalize | `preprocess.normalize` | `none` \| `linear` (x·scale) \| `mean_std` ((x−mean)/std·scale); scale only with linear/mean_std | executed |
| resize | `preprocess.resize: "to_input"` + `input.width/height` | resample window to the fixed graph input; `interpolation`: `bilinear` (default) \| `nearest` | executed |
| pad / clamp | — | NOT implemented; no manifest key exists (a declared key would be rejected — the #646 rule). Aspect-preserving letterbox padding is a documented follow-up | not present |

## Postprocessing — raster tasks (`output.format`)

Derived modes collapse the FIRST head's class planes into one band; the
probability stack keeps all heads (+ optional uncertainty band).

| mode | Product | Notes |
|---|---|---|
| `probability` (default) | float32 class stack (+ `uncertainty`: softmax `entropy` or `margin` band) | `postprocess.mask_threshold` binarizes per class when ≥ 0 |
| `labels` | argmax raster, Byte (≤255 classes) else UInt16, NoData 255/65535, `SICNU_CLASS_PALETTE`/`SICNU_CLASS_NAMES` metadata (deterministic HSV formula, ADR 0061) | regression semantics: a 1-channel model is a continuous float32 raster with NaN valid mask |
| `mask` | binary 0/1 Byte: C==1 → plane ≥ threshold (default 0.5); C>1 → argmax ≠ 0 | |
| `confidence` | top-1 probability band, Float32 | |

Invalid pixels (every input band non-finite) stay at the writer's NoData
sentinel in every mode. Model-emitted NaN in a winning plane keeps the pixel
NoData — NaN is never written as a class value.

## Postprocessing — detection (`output.detection`)

1. decode per tile: layout `xywh_objectness` (score = obj·max(cls)) or
   `xywh_class_scores` (score = max(cls)); tensor shape (1,C,N) or (1,N,C),
   explicit `tensor_layout` or the C ≤ N `auto` heuristic;
2. threshold (`conf_threshold`; `output.threshold` folds in when declared),
   center/size → raster-pixel corner boxes (tile origin × window/tensor scale),
   clamped to the raster;
3. whole-raster NMS (the tile dedup): exact-duplicate collapse + greedy
   suppression at `nms_iou` with deterministic order (confidence desc, ties by
   classId then x, y, w, h — never insertion order);
4. geographic mapping via the raster geotransform; fields
   `class`, `confidence`, `tile_x`, `tile_y`;
5. atomic vector publish (GPKG / GeoJSON / Shapefile by extension).

Accumulation is bounded by `max_detections`; exceeding it fails the run.

## Embedding

An embedding model runs through the same raster path: the feature stack is
the float32 output (NaN = invalid pixels). `rs:embedding aggregate=mean`
adds a per-scene mean feature vector to the RESULT JSON (bounded single-row
GDAL reads; the stack itself is never materialized whole) and reports
`embedding_dim`.

## Test surface

- `tests/test_model_tasks.cpp` — labels/mask/confidence products, regression
  continuous raster, decode layouts, deterministic NMS, seam integration.
- `tests/test_model_runtime.cpp` — probability stack, uncertainty, TTA,
  NoData skip, dtype/shape contracts.
- `tests/test_model_failure_matrix.cpp` — OOM ladder, cancel, atomicity,
  device matrix, pool bounds, registry surface.
