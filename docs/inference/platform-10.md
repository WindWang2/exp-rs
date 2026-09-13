# Model Runtime Platform 10.0 — EO Model Platform

Extends [platform-9.md](platform-9.md). The model runtime stays ONE execution
seam (`runModelInference` → tile/detection/scene engines); 10.0 adds the EO
domain truth layer, three task-family adapters, manifest-driven postprocessing
extensions and optional deployment providers.

## EO manifest truth (`eo` section, manifest_version 6)

```jsonc
{
  "manifest_version": 6,
  "task": "classification",
  "eo": {
    "wavelengths_nm": { "red": [630, 700], "nir": [780, 1400] },
    "calibration": { "state": "toa_reflectance", "enforced": true },
    "grid": { "crs_family": "projected" }
  }
}
```

- `wavelengths_nm`: per-band-role sensitivity windows (nm). Enforced when the
  input band declares a center wavelength (`WAVELENGTH` band metadata): an
  out-of-window band is a typed refusal — the model would misread it
  silently. Missing facts on either side are recorded as advisories, never
  guesses.
- `calibration.state`: one of the SICNU_RADIOMETRIC_STATE tokens (`radiance`,
  `toa_reflectance`, `surface_reflectance`, `brightness_temperature`,
  `digital_number`, `gamma0`, `indices`, `any`). With `enforced: true` the
  preflight is FAIL-CLOSED: an input that declares no state or a different
  state is refused before inference — unverified radiometric domains are
  never fed to the model.
- `grid.crs_family`: `geographic` | `projected` | `any` (default).

All checks/advisories land in the run stats (`eoPreflight`) and the
provenance sidecar. Facts are read from the canonical metadata layer
(`inspectRaster`), so a host without the metadata sees honest advisories —
never fabricated verification.

## Canonical EO tasks and task adapters

`canonicalEoTask()` resolves the manifest `task` to one of `segmentation`,
`detection`, `classification`, `change_detection`, `regression`,
`instance_segmentation`, `embedding`, `super_resolution` (aliases:
`semantic_segmentation`, `object_detection`, `scene_classification`, `ssl`,
`feature_embedding`). Free-form legacy tasks keep parsing; they simply carry
no canonical adapter contract.

| Operator | Canonical task | Artifact contract |
|---|---|---|
| rs:segment / rs:detect / rs:embedding / rs:infer | (4.0 surface) | raster / vector / feature stack |
| rs:classify | classification | `exp-rs-classification/1` JSON: predicted class + probability distribution + model identity + input fingerprint + EO preflight report |
| rs:change | change_detection | change-probability stack over two co-registered feeds (A → inputs[0], B → inputs[1]) |
| rs:regress | regression | continuous-value raster (raw model channels, no argmax) |

rs:classify runs ONE forward pass over the whole scene (bounded: chips up to
2048×2048 px), reducing class planes by spatial mean-pooling; declared logit
heads are softmaxed. Task-intent gates refuse a model whose canonical task
does not match the operator — intent and contract must agree.

## Manifest-driven pre/postprocessing (10.0 additions)

- `preprocess.offset` — additive, applied AFTER scale under linear/mean_std;
  refused under `normalize: none` (declared knobs are never silently ignored).
- `postprocess.calibration_temperature` — probability-space temperature
  scaling `p^(1/T)` (multi-class: normalized) BEFORE the derived collapse.
  Argmax is invariant; mask thresholds and confidence values move
  deliberately. Recorded in the provenance sidecar.
- `postprocess.morphology` (`erode` | `dilate` | `open` | `close`) +
  `morphology_kernel_px` — sentinel-aware cleanup on the published
  labels/mask product as a bounded streaming pass (full-width row bands with
  kernel-radius halo; 2× for the two-op open/close pair). NoData pixels never
  leak into the neighborhood; memory is O(W·rows).

## Optional providers

| Framework | Token | Enable | Absent behavior |
|---|---|---|---|
| TensorRT (prebuilt .engine/.plan) | `tensorrt` | `-DSICNU_ENABLE_TENSORRT=ON` + SDK | typed `UnsupportedRuntime` |
| OpenVINO (IR/ONNX) | `openvino` | `-DSICNU_ENABLE_OPENVINO=ON` + SDK | typed `UnsupportedRuntime` |

Default builds are unchanged: the provider TUs degrade to stubs unless the
SDK is found, and `ModelRuntimeRegistry::registerProvider` remains the plugin
provider seam.

## MLOps seam

`verifyProductAgainstModel(outputPath, modelReference)` resolves the model
through the catalog's readiness pipeline and verifies the product's
`exp-rs-prov/1` sidecar against its identity tag, content digest and backend
— the single entry point for benchmark sets, promotion evidence and replay
deviation checks.

## Model selection (agent knowledge)

Model choice is a manifest-FACTS decision, never a name guess: sensors,
band roles, wavelength windows, resolution range, modality, temporal shape,
memory profile, canonical task and output semantics are all declared in the
manifest (and projected via `ModelInfo::toJson` / `spatial:list_models`). See
`pi/knowledge/spatial-algorithm-guide.md` for the selection workflow.
