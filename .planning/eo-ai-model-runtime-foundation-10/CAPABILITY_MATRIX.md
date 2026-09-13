# CAPABILITY_MATRIX — eo-ai-model-runtime-foundation-10

Legend: ✅ exists+verified on master @ `7d78059d1a` · 🟡 partial (audit note) · ❌ absent · 🆕 this track's deliverable.

## A — Manifest truth layer

| Capability | State | Note |
|---|---|---|
| model id/version/digest | ✅ | ModelInfo stableId/identityTag/contentDigest/packageDigest |
| task field | 🟡 | free-form string; no closed EO vocabulary, no per-task contract mapping |
| framework/provider | ✅ | runtime.framework + provider contract (http/python) |
| modality per feed | ✅ | inputs[].modality (optical/sar/dem/mask/aux) |
| sensor compatibility | ✅ | domain.sensors (list only; no per-sensor band/wavelength binding) |
| band roles | ✅ | inputs[].band_roles |
| wavelengths | ❌ | 🆕 eo.wavelength_nm ranges per band role; validated against sensor band mapping |
| GSD/resolution range | ✅ | domain.resolution_range [min,max] meters |
| CRS/grid assumptions | 🟡 | inputs[].alignment="reference" (co-registration) only; 🆕 eo.grid (crs family, alignment policy) |
| temporal input shape | ✅ | temporal_length/collapse/dynamic |
| preprocessing per feed | ✅ | inputs[].preprocess |
| normalization | ✅ | none/linear/mean_std + scale + clamp + pad |
| chip/tile + overlap/halo + batch | ✅ | tiling contract |
| dynamic/static shape | ✅ | input.width/height (0=dynamic) |
| output type/classes/thresholds | ✅ | output contract + detection contract |
| uncertainty | ✅ | output.uncertainty entropy/margin |
| postprocess | 🟡 | nms/mask_threshold/polygonize/simplify/class_mapping; 🆕 morphology + calibration |
| memory profile | ✅ | runtime.estimated_vram_mb/ram_mb |
| license/provenance | ✅ | license/source/reference |
| calibration requirements | 🟡 | radiometricState string; 🆕 typed eo.calibration requirement + runtime check hook |

## B — Task families

| Family | State | Note |
|---|---|---|
| semantic segmentation | ✅ | rs:segment |
| object detection | ✅ | rs:detect (fix F-OPS-5 in scope) |
| classification | ❌ | 🆕 rs:classify (per-scene class + probabilities artifact) |
| change detection | 🟡 | multi-input siamese via rs:infer; no task adapter, no typed artifact | 🆕 rs:change |
| regression | 🟡 | output semantics exist (RegressionRuntime test); no adapter | 🆕 rs:regress |
| instance segmentation | ❌ | 🆕 instance decode (connected components → vector artifact w/ attributes) |
| super-resolution | ❌ | deferred: no scientific driver in-repo; record decision |
| optical+SAR multimodal | ✅ | named multi-input feeds + per-feed preprocess |
| temporal EO | ✅ | NCTHW/dynamic-T |
| hyperspectral | 🟡 | band-count agnostic; no EO metadata binding | 🆕 wavelength contract covers |
| embedding | ✅ | rs:embedding |
| vision-language EO | ❌ | no in-repo model class; record decision (text-image feeds need tokenizer contracts) |
| promptable (SAM-like) | 🟡 | SAM manifests exist as segmentation templates; no prompt surface | 🆕 geo-prompt seam on rs:segment (point/box hint → mask window) documented+refusal contract |

## C — Providers

| Provider | State |
|---|---|
| ORT CPU | ✅ |
| ORT CUDA | ✅ (9.0 real execution) |
| OpenCV DNN | ✅ |
| Python worker | ✅ |
| HTTP | ✅ |
| TensorRT | ❌ → 🆕 optional adapter |
| OpenVINO | ❌ → 🆕 optional adapter |
| plugin registration | 🟡 registry.registerProvider is public; 🆕 documented seam + SDK projection |

## D — Large raster / lifecycle

| Capability | State |
|---|---|
| tile plan/halo/batch/OOM ladder/atomic output/cancel | ✅ |
| feather blending | ✅ |
| class statistics + uncertainty maps | ✅ |
| resume after interruption | ❌ → 🆕 checkpoint sidecar + skip-completed-tiles |
| benchmark set + promotion evidence | 🟡 payload exists; 🆕 experiment-store record |
| stale package detection | ✅ (digest at resolve) |
| replay deviations | 🟡 verifyProductProvenance exists; 🆕 wiring |

## E — Selection knowledge / security

| Capability | State |
|---|---|
| rankModels(criteria) | ✅ (task/sensor/bandRoles/resolution/gpu/vram) |
| manifest-facts projection for agents | 🟡 spatial:list_models; 🆕 capability knowledge file + deterministic-grade + output meaning |
| python worker bounded streams/timeout/cleanup | ✅ (9.0) — re-audited this track |
| secret redaction / path trust / no shell | 🟡 → audit + fixes this track |
