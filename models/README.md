# Model Runtime Catalog (ADR 0122)

Local registry of inference model runtimes for `rs:infer` and the agent
`spatial:list_models` tool. Each subdirectory carries a `model.json`
manifest; weight files are referenced by relative path and are **not**
committed to the repository — drop the downloaded weights next to the
manifest and set `"path"`.

## Manifest schema

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

Resolution order for the catalog root: `$SICNU_MODELS_DIR`, `<cwd>/models`,
`<application dir>/../models`.

## Registered templates

| Name | Task | Contract | Framework | Weights |
|---|---|---|---|---|
| `sam-building` | segmentation | raster → polygon | onnx | not bundled — template manifest |
| `yolo-buildings` | detection | raster → vector | onnx | not bundled — template manifest |

To activate a template: download the ONNX weights into the model directory,
set `"path"` to the file name, and verify with `spatial:list_models`
(name lookup) and `rs:infer` (execution).
