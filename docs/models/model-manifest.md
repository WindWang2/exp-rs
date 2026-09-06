# Model Manifest Reference (Platform 4.0)

A model is a directory under `models/` containing `model.json`. Weights are
referenced by path and are never committed to the repository. The manifest is
the single source of truth for discovery, identity, contracts and runtime
selection; GUI, CLI, Workflow, MCP/Pi and the SDK reference models by stable
id — never by weight file path.

Discovery: `$SICNU_MODELS_DIR` → `<cwd>/models` → `<app dir>/../models`.
The registry (`ModelCatalog`) is authoritative: `find(id)` / `resolve("id@version")`
/ `inspect(id)` / `health(id)` / `validateManifestJson(json)` /
`registerManifestJson(json, source)` (session-scoped, plugins/tests).

## Manifest versions

`manifest_version` (1–4) is optional. When declared it must match the
manifest's shape — `inputs` array ⇒ 3, `input` object ⇒ 2, flat fields ⇒ 1 —
otherwise the manifest is invalid (`InvalidManifest`). Platform 4.0 fields
below parse under every shape; declaring `manifest_version: 4` documents the
intent to use them.

## Identity

| Field | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | required | Unique catalog name (display + legacy reference) |
| `id` | string | `name` | **Stable identity** used by all surfaces; unique |
| `model_version` | string | `"0"` | Model version string |
| `license` | string | unspecified | SPDX expression or name |
| `source` | string | — | Provenance origin (URL / download source) |
| `manifest_version` | int | inferred | Declared manifest generation (1–4) |

`id@version` resolves exactly; a bare `id` resolves the sole entry (or the
lexicographically latest version when several exist).

**Artifact content identity**: the SHA-256 of the weight bytes
(`content_digest`) is computed at catalog load — checksum declared or not —
and anchors session identity. The same path with different bytes is a
different model for the runtime session cache; `inspect(id)` exposes the
digest. Declaring `artifact.checksum` additionally *enforces* the digest
(`ChecksumMismatch` readiness on mismatch).

## Contracts (all sections optional; absent = documented default)

- `artifact`: `path` (manifest-dir relative or absolute), `checksum`
  (`sha256:<hex>` or bare hex), `size_bytes` (0 = unchecked).
- `input` (v2) / `inputs` (v3 array): `name`, `data_type`, `dtype`, `layout`
  (NCHW executed; others rejected), `band_roles`, `width`, `height` (fixed
  graph input), `temporal_length`, `temporal_collapse` (`channels` only;
  T-frame execution is not wired and fails loudly at run).
- `preprocess`: `normalize` (`none` | `linear` | `mean_std`), `mean`, `std`,
  `scale` (linear/mean_std only), `resize` (`none` | `to_input`), `interpolation`
  (`bilinear` | `nearest`), `nodata_policy` (`zero` only).
- `tiling`: `supported`, `tile_size`, `overlap`, `halo` (≤ tile_size/2),
  `batch_size` (1–64).
- `output`: `type`, `tensor_names`, `classes`, `uncertainty`
  (`none` | `entropy` | `margin`), `format` (see below), `detection` (see below).
- `postprocess`: `mask_threshold` (probability binarization), `nms`,
  `polygonize`, `simplify` — see execution status below.
- `runtime`: `gpu`, `cpu_fallback`, `estimated_ram_mb`, `estimated_vram_mb`,
  `supports_tiling`, `device` (`cpu` | `cuda` | `cuda:N` | `auto`).

## `output.format` — raster-task products

| format | Product | DType |
|---|---|---|
| `""` / `probability` | float32 per-class stack (default; regression/embedding path) | Float32 |
| `labels` | argmax class raster + `SICNU_CLASS_PALETTE` / `SICNU_CLASS_NAMES` metadata | Byte ≤255 classes, else UInt16; NoData 255/65535 |
| `mask` | binary 0/1 (C==1: plane ≥ `mask_threshold` (default 0.5); C>1: argmax ≠ 0) | Byte; NoData 255 |
| `confidence` | top-1 probability band | Float32; NoData NaN |

Conflicts fail loudly: uncertainty + derived format, `mask_threshold` +
`labels`, multi-head + derived format.

## `output.detection` — detection decode contract

Declaring it makes a model detection-executable (vector output) and unlocks
`postprocess.nms` / `output.threshold` (they fold into the contract).
Without it those fields stay rejected as unimplemented (`#646` rule).

| Field | Default | Meaning |
|---|---|---|
| `layout` | `xywh_objectness` | `xywh_objectness` (cx,cy,w,h,obj,cls…) or `xywh_class_scores` (cx,cy,w,h,cls…) |
| `tensor_layout` | `auto` | `channels_first` (1,C,N), `channels_last` (1,N,C), `auto` (C ≤ N heuristic) |
| `conf_threshold` | 0.25 | score gate; v5 score = obj·max(cls), v8 score = max(cls) |
| `nms_iou` | 0.45 | whole-raster NMS / tile-dedup IoU, (0,1] |
| `max_detections` | 100000 | bounded accumulation guard |
| `classes` | output.`classes` | class channel names |

## Runtime selection

`runtime.device` semantics (deterministic; see
[device-and-memory-policy.md](../inference/device-and-memory-policy.md)):
`auto` resolves to the lowest addressable CUDA device that fits the VRAM
budget, else CPU. `cuda:N` beyond the backend's addressable indices fails
loudly. Legacy `gpu: true` behaves as `auto` with GPU preference.

## Readiness states

`Ready` · `MissingArtifact` (no/unreadable artifact) · `ChecksumMismatch`
(size or digest) · `InvalidManifest` (contract violation). Catalog-static
readiness is extended by the runtime layer with `UnsupportedRuntime` (no
provider for the framework) and `IncompatibleHardware` (device/VRAM verdict).

## Example (4.0)

```json
{
  "name": "yolo-ship-detection",
  "id": "rs/yolo-ship-detection",
  "model_version": "1.0.0",
  "license": "unspecified",
  "source": "https://example.com/yolo-ship",
  "manifest_version": 2,
  "task": "detection",
  "framework": "onnx",
  "input": { "dtype": "float32", "band_roles": ["red","green","blue","nir"],
             "width": 640, "height": 640 },
  "preprocess": { "normalize": "linear", "scale": 0.00392, "resize": "to_input" },
  "tiling": { "supported": true, "tile_size": 640, "overlap": 96, "batch_size": 4 },
  "output": {
    "type": "vector",
    "classes": ["ship"],
    "detection": { "layout": "xywh_objectness", "tensor_layout": "auto",
                   "conf_threshold": 0.25, "nms_iou": 0.45,
                   "max_detections": 100000, "classes": ["ship"] }
  },
  "runtime": { "gpu": false, "cpu_fallback": true, "device": "auto" }
}
```

Pre/post-processing semantics: [pre-post-processing.md](../inference/pre-post-processing.md).
Model authoring guide: [model-authoring.md](../inference/model-authoring.md).
