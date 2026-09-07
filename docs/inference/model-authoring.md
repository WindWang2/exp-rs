# Model Import / Authoring Guide

How to bring a trained model into the platform. The short version: one
directory, one manifest, weights beside it — everything else is derived.

## 1. Directory layout

```text
models/
  my-unet-water/
    model.json        # the manifest (authoritative)
    weights.onnx      # ONNX readable by cv::dnn (or your framework's format)
```

`models/` resolves via `$SICNU_MODELS_DIR`, `<cwd>/models`, or the
application-relative `../models`. Set `artifact.path` and (strongly
recommended) `artifact.checksum` (`sha256:<hex>`) + `size_bytes` after
downloading weights. Without a checksum the bytes are still hashed for
identity — but a declared checksum is what *enforces* integrity.

## 2. Minimal manifest (raster segmentation)

```json
{
  "name": "my-unet-water",
  "id": "local/my-unet-water",
  "model_version": "1.0.0",
  "license": "CC-BY-4.0",
  "task": "segmentation",
  "framework": "onnx",
  "artifact": { "path": "weights.onnx", "checksum": "sha256:...", "size_bytes": 0 },
  "input": { "dtype": "float32", "layout": "NCHW", "band_roles": ["red", "green", "blue", "nir"] },
  "preprocess": { "normalize": "mean_std", "mean": [0.1, 0.2, 0.3, 0.4], "std": [0.1, 0.1, 0.1, 0.1] },
  "tiling": { "supported": true, "tile_size": 512, "overlap": 64, "batch_size": 2 },
  "output": { "type": "raster", "classes": ["background", "water"], "uncertainty": "entropy" },
  "runtime": { "gpu": true, "cpu_fallback": true, "estimated_vram_mb": 2048 }
}
```

Field-by-field reference: [../models/model-manifest.md](../models/model-manifest.md).

## 3. Detection model

Add a fixed graph input and the decode contract; the model becomes
vector-executable through `rs:detect`:

```json
{
  "input": { "width": 640, "height": 640 },
  "preprocess": { "resize": "to_input", "normalize": "linear", "scale": 0.00392 },
  "output": {
    "type": "vector",
    "classes": ["ship"],
    "detection": { "layout": "xywh_objectness", "tensor_layout": "auto",
                   "conf_threshold": 0.25, "nms_iou": 0.45, "max_detections": 100000 }
  }
}
```

Export notes: `xywh_objectness` expects channels (cx, cy, w, h, objectness,
class scores…) per candidate (YOLOv5-style); `xywh_class_scores` expects
(cx, cy, w, h, class scores…) without objectness (YOLOv8-style). If your
export emits (1, N, C), declare `tensor_layout: "channels_last"` — do not
rely on `auto` for degenerate tensors.

## 4. Validate before shipping

```bash
# headless check (after build)
ctest -R test_model_library_manifests --output-on-failure   # shipped templates
```

Programmatically, `ModelCatalog::validateManifestJson(json)` returns the
issue list without registering. Readiness (`MissingArtifact`,
`ChecksumMismatch`, `InvalidManifest`) is reported by `spatial:list_models`
and `health(id)`; runs refuse non-ready models with the reason.

## 5. Choosing the output product

- Per-class probabilities (GUI rendering, post-classification chains): keep
  `format` unset (probability stack).
- Class map for area statistics: `"format": "labels"` (argmax raster with a
  deterministic palette).
- Binary mask (footprints, water): `"format": "mask"` + `postprocess.mask_threshold`.
- Uncertainty-aware QA: `"uncertainty": "entropy" | "margin"` (probability mode).
- Vector detections: `output.detection` (see above).

## 6. Tiling guidance

- `tile_size`: the model's receptive context (commonly 512–1024); must be ≥ 2×
  `halo`. Fixed-shape graphs usually want `resize: to_input` + explicit
  `input.width/height` and a tile size matching the input.
- `overlap`/`halo`: context around each core tile; grid-preserving models get
  seams removed by cropping. Detection models use overlap for context —
  duplicates resolve by whole-raster NMS.
- `batch_size`: throughput knob; VRAM budget clamps it automatically, and the
  OOM ladder degrades to 1 before failing (memory policy:
  [device-and-memory-policy.md](device-and-memory-policy.md)).

## 7. What NOT to put in a manifest

- Absolute paths as identity — the stable `id` is the reference; artifact
  paths are locators.
- Knobs the runtime does not implement (today: `polygonize`, `simplify`,
  `nodata_policy` values other than `zero`, layouts other than NCHW) — they
  are rejected loudly at parse instead of being silently ignored.
- Weights in git — the manifest documents `source`; weights stay out of the
  repository (enforced by the shipped-template gate test).
