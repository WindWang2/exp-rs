# BASELINE — Model Runtime & Multimodal EO Inference 9.0

Branch `feat/model-runtime-multimodal-9`, worktree `exp-rs-model-runtime-9`,
based on `origin/master` @ `132da5e998eca004d43285d1f71565f973030f5f`
(merge of PR #847 geospatial-data-fabric-8, 2026-09-11).
CI/CD is NOT a completion condition; local reproducible evidence only.

## 1. Repository state (verified 2026-09-11)

- `origin/master` = `132da5e998`; recent merges: all eight 8.0 platform
  tracks (#839–#847), including **#837 "AI Model Runtime & Multimodal EO
  Inference Platform 8.0"** (`feat/model-runtime-8`).
- Open PRs: **none**.
- Open issues: #848–#882 (35). The MAIN worktree (local `master`
  `8f6293bceb` + uncommitted diff, NOT pushed) carries another track's
  in-flight remediation of #848–#882 (terrain flow, task center, workbench,
  help, cartography...). It also touches `rs_inference_operator.cpp`
  (#872 fix) — overlap risk handled in OVERLAP_MAP.md.
- Local-only master commit `8f6293bceb` "fix(core): resolve P0 defects
  (#848-#852)" is unpushed; my branch is intentionally NOT based on it.

## 2. Host truth (verified 2026-09-11)

| Item | Value |
|---|---|
| GPU | NVIDIA GeForce RTX 3080 Laptop, 16384 MiB, SM 8.6 (Ampere) |
| Driver | 610.57.04, CUDA UMD 13.3 (`/dev/nvidia0` present) |
| NVML | `libnvidia-ml.so` available (in-process device query possible) |
| ORT SDK | `~/.local/opt/onnxruntime-linux-x64-1.20.1` — **CPU-only build** (no `libonnxruntime_providers_cuda.so`) |
| cuDNN | **absent** (no libcudnn anywhere on host) |
| CUDA runtime | 13.3 (`/opt/cuda/lib64`, libcudart.so.13) — ORT 1.20.1 GPU needs CUDA 12 + cuDNN 9 |
| Toolchain | gcc `/usr/sbin/c++`, Ninja, ccache (78% hit), 16 cores / 62 GB RAM |
| Qt | Qt6 system package |

Consequence: a real GPU exists, but the CUDA Execution Provider cannot run
with the CPU-only ORT SDK. 9.0 therefore (a) adds **real NVML-backed device
inventory** (validated against the real GPU), (b) attempts a user-local GPU
ORT stack (pip `onnxruntime-gpu` pulls CUDA-12 + cuDNN-9 pip wheels), and
(c) if the stack cannot load, keeps CUDA EP **capability-gated with typed
refusal + honest not-run marking** — never a fake PASS. Environment
overrides (`SICNU_MODEL_*`) remain test seams; when NVML is available the
REAL numbers win.

## 3. What 8.0 delivered (from PR #837 + its planning evidence)

Verified seams in `src/operators/runtime/` (8.3k LOC) + `model_catalog.*`:

- Real ORT lane (1.20.1 CPU): named multi-input, N-D, dynamic shapes,
  multi-head, warmup/health/memory, cancel-before + in-forward cancel,
  typed CUDA refusal (GPU-gated skip).
- Providers: opencv-dnn ("onnx"), onnxruntime, http, python worker;
  `exp-rs-infer/1` wire + handshake capability negotiation; bounded
  respawn+replay after worker death.
- Device planner: DeviceInventory + VramLedger + resolveDevice (3
  overloads incl. LeastLoaded placement policy), env-based inventory,
  pressure valve (one bounded eviction pass), deviceReport().
- TileInferenceEngine: halo/overlap tiling, OOM ladder (batch halving),
  TTA, nodata tile skip, output modes (probability/labels/mask/confidence),
  uncertainty (entropy/margin), multimodal `runMultiInput` with grid+CRS
  authority (never implicit warp), temporal channels-collapse + rank-5
  sequence, strictly-increasing timestamps, quality masks, 1024-frame
  bound, atomic publish + `exp-rs-prov/1` sidecar.
- DetectionTileEngine + detection_postprocess (conf gate, NMS IoU,
  class mapping on labels).
- ModelCatalog: 24 shipped manifests, readiness, health, digest session
  keys, stable ids.
- Tests: 14+ suites (test_model_runtime*, test_onnxruntime_provider,
  test_multimodal_inference, test_tensor_blob, test_provider_http,
  test_provider_python, test_model_catalog_v2, test_model_manifest7,
  test_model_library_manifests, test_model_failure_matrix,
  test_model_tasks, test_change_detection, bench suite).

## 4. Known gaps 9.0 must close (from 8.0 FINAL_REPORT + code audit)

1. CUDA EP never executed on real hardware; no real device inventory
   source (env vars only) — M1/M2.
2. No consumer-side provenance detector (sidecar can be MISSING after a
   crash window; consumers can't verify staleness) — M8.
3. Model packaging identity: digest = sha256(weights) only; auxiliary
   files (pre/post config, class ontology) not digest-bound — M7.
4. Feed-level preprocessing normalization (per-feed stats/scale/band
   mapping) and input fingerprinting — M3.
5. Tile blending: engine stitches core tiles hard-edge (no overlap
   blending/multi-band blending) — M5 (verify + close).
6. Deterministic dedup for detection across tiles; segmentation
   confidence semantics — M5/M6 (audit first).
7. Embedding product semantics — M5/M6.
8. Temporal batching (T>1 in ONE forward when the graph allows) — M4.
9. Provenance execution identity (provider versions) — M8.
10. #872 schema drift (`device` missing from rs:infer schema props) —
    still-valid on origin/master.

## 5. Baseline build evidence

- Configure: `-G Ninja -DCMAKE_BUILD_TYPE=Release -DSICNU_WITH_ONNX_RUNTIME=ON
  -DONNXRUNTIME_ROOT_DIR=$HOME/.local/opt/onnxruntime-linux-x64-1.20.1`
- `sicnu-operators` target builds green (ninja -j4, ccache warm).
- Targeted test run: see TEST_MATRIX.md (updated per milestone).
