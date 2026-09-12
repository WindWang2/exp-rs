# CAPABILITY_MATRIX — model-runtime-multimodal-9

Legend: ✅ exists+verified · 🟡 partial (audit note) · ❌ absent on
`origin/master` @ `132da5e998` · 🆕 9.0 deliverable.

## M0 — Contract & runtime truth

| Capability | State | Note |
|---|---|---|
| rs:infer runtime contract self-consistency | 🟡 | #872: `device` parsed+output-declared but absent from schema properties |
| Mechanical schema ⊇ parsed-params truth test | ❌ | 🆕 regression for the drift class on model operators |
| Error taxonomy + error-code projection | ✅ | `classifyInferenceError` + `errorCodeForInferenceFailure`; extend additively |
| Real-vs-compiled provider marking | 🟡 | 8.0 marked honestly; 9.0 replaces env-only truth with REAL host evidence |

## M1 — Provider matrix

| Capability | State | Note |
|---|---|---|
| ORT CPU lane | ✅ | 1.20.1 evidence (8.0); 9.0 re-verified on 1.30 GPU SDK |
| CUDA EP **real execution** | ❌→🆕 | Host has RTX 3080 (16GB, SM8.6); pip ORT 1.30 GPU + cuDNN verified Conv-on-GPU. 9.0 wires the C++ provider to it: capability-gated tests, real forwards, honest refusal when stack absent |
| Python worker provider | ✅ | restart budget, capability negotiation; 9.0: GPU capability declaration via same venv, timeout taxonomy |
| HTTP provider | ✅ | timeout/cancel audit in M1 |
| Provider handshake capabilities | ✅ | `exp-rs-infer/1` + capability block |
| Crash/restart bounded replay | ✅ | worker: ONE respawn+replay; extend to CUDA provider paths |
| Deterministic error taxonomy across providers | 🟡 | audit CUDA/cuDNN load failures → typed kinds (DeviceUnavailable/ProviderCrash) |
| Cold/warm acquire + session metrics | ✅ | health/poolStats; add per-provider warm evidence |

## M2 — Device & memory planner

| Capability | State | Note |
|---|---|---|
| cpu/cuda:N/auto tokens | ✅ | strict parse, typed refusal |
| **Real device inventory** | ❌→🆕 | env-only today; 🆕 NVML-backed enumeration (name/total/free VRAM/compute cap) with env still as test override |
| Per-device VRAM ledger | ✅ | admission authority; 🆕 verify against REAL free VRAM |
| Placement policy (LowestFitting/LeastLoaded) | ✅ | 8.0 WP-B |
| Pressure valve (bounded eviction) | ✅ | one pass per failed admission |
| **OOM ladder** | ✅ | batch-halving in tile engine; 🆕 CUDA OOM evidence real (TF/TRT messages classification) |
| GPU-unavailable typed refusal | ✅ | forced-CUDA test; stays |
| Dynamic pressure re-check | 🟡 | snapshot exists; 🆕 refresh free VRAM from NVML at acquire decision time |

## M3 — Multimodal feed graph

| Capability | State | Note |
|---|---|---|
| Named multi-input feeds | ✅ | `runMultiInput` + manifest inputs[] |
| Grid + CRS authority, never implicit warp | ✅ | 8.0 WP-C |
| Per-feed band mapping | ✅ | feed.bands (1-based selection) |
| **Per-feed normalization** | ❌ | preprocess contract is GLOBAL today (audit: line-level evidence); 🆕 inputs[].preprocess overrides |
| Strict duplicate/missing feed rules | 🟡 | missing frames policy exists; duplicate feed NAMES audit 🆕 |
| **Input fingerprint** | ❌ | 🆕 content fingerprint per feed into payload+sidecar |
| Alignment provenance (preparedFrom) | ✅ | 8.0 |
| Quality masks | ✅ | 8.0 WP-D |

## M4 — Temporal

| Capability | State | Note |
|---|---|---|
| Rank-5 NCTHW sequence | ✅ | 8.0 WP-D |
| Variable T (feed-defined) | ✅ | temporal_dynamic |
| Strictly increasing timestamps | ✅ | refuse, never sort |
| Bounded T | ✅ | 1024 |
| Missing frames policy | ✅ | refuse | zero-fill |
| **Temporal batching (T>1 forwards)** | ❌→🆕 | today every frame folds into ONE forward (T·C channels); 🆕 chunked T windows for graphs with fixed C |
| STAC/local collection expansion | ✅ | operator surface, local docs |
| Per-frame provenance | ✅ | preparedFrom parallel arrays; sidecar records |
| No unbounded whole-series memory | ✅ | windowed reads; 🆕 regression for variable-T chunking |

## M5 — Tile engine

| Capability | State | Note |
|---|---|---|
| Tile planner + halo + overlap | ✅ | |
| **Overlap blending (feather)** | ❌ | core tiles hard-stitched; 🆕 weight blending in halo (cosine/equal), provenance notes method |
| Batch + OOM ladder + edge tiles + nodata skip | ✅ | |
| Cancellation + progress | ✅ | per batch; in-forward for ORT |
| Memory budget | ✅ | effectiveBatchSize budget-aware |
| 100k logical extents | 🟡 | windowed by design; 🆕 stress evidence at 1e5 px extents |
| Output atomicity | ✅ | staging+rename+backup |
| Deterministic detection dedup | ✅ | exact-dup collapse + whole-raster NMS |
| **Embedding products** | 🟡 | rs:embedding operator exists (execution path audit M5); 🆕 defined tensor product semantics |
| Segmentation confidence | 🟡 | confidence mode exists; per-class metadata audit M6 |

## M6 — Postprocess & products

| Capability | State | Note |
|---|---|---|
| labels/mask/confidence/probability | ✅ | output modes |
| Detection vector output + CRS | ✅ | DetectionTileEngine |
| Class mapping + palette | ✅ | 8.0 WP-E |
| NMS + confidence gate | ✅ | operator overrides |
| Uncertainty (entropy/margin) | ✅ | |
| Threshold (mask) | ✅ | mask_threshold |
| **Per-class metadata in products** | 🟡 | palettes exist; per-class names/areas metadata audit 🆕 |
| NoData semantics in products | ✅ | nodata reconstruction |
| Atomic publication + previous-good | ✅ | + sidecar ordering |
| Sidecar consistency | 🟡 | write-order safe; consumer-side missing in M8 |

## M7 — Packaging & identity

| Capability | State | Note |
|---|---|---|
| stable id/version | ✅ | catalog |
| content digest (weights) | ✅ | sha256; session key |
| **Auxiliary-file identity** | ❌ | 🆕 manifest-referenced aux files (class ontology, pre/post config) digest-bound |
| Manifest validation | ✅ | closed vocab + validators |
| Provider compatibility declaration | 🟡 | runtime.framework vs provider registry; 🆕 explicit min/max provider API note |
| Reproducible catalog scan | ✅ | catalog tests |
| Duplicate/version conflict | 🟡 | audit catalog duplicate-id behavior 🆕 |

## M8 — Provenance bridge

| Capability | State | Note |
|---|---|---|
| `exp-rs-prov/1` sidecar write | ✅ | model/device/grid/preprocess/counters |
| **Consumer-side verification API** | ❌ | 🆕 `verifyProvenance(output, sidecar)` — presence, schema, model digest match, grid match, staleness (mtime/digest), typed verdicts |
| Execution identity (provider version) | ❌ | 🆕 backend version strings in sidecar |
| Evidence seam for Experiment/MLOps | 🟡 | payload exists; 🆕 stable accessor for the mlops track |

## M9 — Performance

| Capability | State | Note |
|---|---|---|
| Bench suite | ✅ | 8.0 WP-H (CPU numbers) |
| **CPU vs CUDA real numbers** | ❌→🆕 | same fixtures, both EPs, honest environment record |
| VRAM pressure / concurrent sessions | 🟡 | ledger tests; 🆕 real-GPU concurrency evidence |
| 100k-extent stress | ❌ | 🆕 logical-extent evidence |
