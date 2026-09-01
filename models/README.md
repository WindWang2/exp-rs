# Model Runtime Catalog (ADR 0122)

Local registry of inference model runtimes for `rs:infer` and the agent
`spatial:list_models` tool. Each subdirectory carries a `model.json`
manifest; weight files are referenced by relative path and are **not**
committed to the repository — drop the downloaded weights next to the
manifest and set `"artifact": { "path": ... }` (or the legacy top-level
`"path"`).

Relative artifact paths resolve against the **manifest directory**, never
the process working directory.

## Readiness states

Every catalog entry carries a real availability state (exposed by
`spatial:list_models` and `ModelInfo::toJson()` as `readiness`):

| State | Meaning |
|---|---|
| `ready` | Manifest parsed, artifact present, checksum/size verified |
| `missing_artifact` | `path` empty (template) or the weight file does not exist |
| `invalid_manifest` | Unparseable/internally inconsistent contract |
| `checksum_mismatch` | Artifact present but digest/size contradicts the manifest |
| `unsupported_runtime` | Framework has no provider in this build |
| `incompatible_hardware` | GPU required, unavailable, CPU fallback disabled |

The first four are computed by `ModelCatalog` at load time; the last two are
runtime-layer verdicts (model runtime registry). Models that are not `ready`
rank incompatible in `spatial:list_models` ranking and `rs:infer` refuses
them with the readiness explanation.

## Manifest schema

### v1 (backward compatible)

```json
{
  "name": "sam-building",       // required — unique; usable directly as rs:infer "model" param
  "task": "segmentation",       // segmentation | classification | detection | ...
  "input": "raster",            // input contract
  "output": "polygon",          // output contract
  "framework": "onnx",          // runtime (onnx via cv::dnn today)
  "path": "sam-building.onnx",  // weight file (relative to the manifest or absolute)
  "gpu": true,                  // GPU expected
  "accuracy": 0.89,             // optional benchmark in [0, 1]
  "description": "...",
  "tags": ["buildings"]
}
```

### v2 (inference contract — all sections optional)

```json
{
  "name": "sam-building",
  "task": "segmentation",
  "framework": "onnx",
  "artifact": {
    "path": "sam-building.onnx",
    "checksum": "sha256:<64 hex>",   // verified at catalog load
    "size_bytes": 123456789
  },
  "input": {                          // object form (string form still accepted)
    "data_type": "raster",
    "band_roles": ["Red", "Green", "Blue", "NIR"],
    "dtype": "float32",
    "layout": "NCHW",
    "width": 512,                     // fixed graph input (omit when dynamic)
    "height": 512
  },
  "preprocess": {
    "normalize": "mean_std",          // none | linear | mean_std
    "mean": [0.485, 0.456, 0.406],    // per band_role, in PIXEL units
    "std":  [0.229, 0.224, 0.225],
    "scale": 1.0,                     // applied LAST as (x-mean)/std*scale; keep 1.0 with mean_std
    "resize": "to_input",             // none | to_input (requires input.width/height)
    "interpolation": "bilinear",      // bilinear | nearest
    "nodata_policy": "zero"           // zero (non-finite input → 0 before the model)
  },
  "tiling": {
    "supported": true,
    "tile_size": 512,
    "overlap": 32,                    // adjacent-tile overlap (halo = overlap/2 per side)
    "batch_size": 4                   // tiles per forward pass
  },
  "output": {
    "type": "raster",
    "tensor_names": ["probability"],
    "classes": ["background", "building"],
    "threshold": 0.5
  },
  "postprocess": {
    "nms": false,
    "mask_threshold": 0.5,            // probability → binary mask
    "polygonize": false,              // chain with gdal:polygonize for vectors
    "simplify": 0.0
  },
  "domain": {
    "sensors": ["Sentinel-2", "GF-2"],
    "resolution_range": [0.3, 10.0]
  },
  "runtime": {
    "gpu": true,
    "cpu_fallback": true,
    "estimated_ram_mb": 768,          // feeds ExecutionPlane/TaskCenter admission
    "estimated_vram_mb": 2048
  }
}
```

## Registered templates

| Name | Task | Contract | Framework | Weights |
|---|---|---|---|---|
| `sam-building` | segmentation | raster → polygon | onnx | not bundled — template manifest |
| `yolo-buildings` | detection | raster → vector | onnx | not bundled — template manifest |

To activate a template: download the ONNX weights into the model directory,
set `artifact.path` (and ideally `checksum`), and verify with
`spatial:list_models` (`readiness: ready`) and `rs:infer` (execution).

Resolution order for the catalog root: `$SICNU_MODELS_DIR`, `<cwd>/models`,
`<application dir>/../models`.
