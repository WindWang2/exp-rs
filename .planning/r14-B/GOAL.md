# R14-B Goal — 既有 AI 模型推理、GPU/CPU 运行时与分块流水线加固

Single direction: harden the EXISTING model-inference stack (providers, GPU/CPU
sessions, tiling, detection postprocess, VRAM ledger, chunk scratch, publish).
No new product directions, no new model lines.

## Verified work packages (each: reproduce → fix minimally → GREEN ×2 → review)

| WP | Item | Severity | Source |
|----|------|----------|--------|
| WP-A1 | NaN geometry boxes pass decode filters → NaN coords published + `std::sort` UB + `lround(NaN)` UB | P1 | audit (verified in code) |
| WP-A2 | `decodeDetections` accepts batch dim > 1 (silently decodes sample 0) | P2 | audit (verified) |
| WP-A3 | `tensorLayout:"auto"` ambiguous when C == N (transposed decode) | P2 | audit (verified) |
| WP-B1 | Strided heads: halo margin not cropped before core resize (single+multi engine) → spatially shifted output | P1 | audit (verified) |
| WP-B2 | Morphology re-tally ADDS to pre-morph tally (doubled `class_pixel_counts`) | P1 | audit (verified) |
| WP-B3 | Stitched Mask mode never tallies classPixelCounts | P1 | audit (verified) |
| WP-B4 | bandCount > CV_CN_MAX(512) wraps cv::Mat type → garbage + OOB | P1 | audit (verified) |
| WP-B5 | Multi-input engine lacks N==B check → raw cv::Exception escapes | P2 | audit (verified) |
| WP-B6 | Morphology failure paths leave `.morph~` / stage residue | P2 | audit (verified) |
| WP-C1 | OpenCV `inferMulti` returns net-internal aliases (race) + setInput outside try + no failure record | P1 | audit (verified) |
| WP-C2 | HTTP provider hangs forever on threads without a Qt event dispatcher | P1 | audit (verified) |
| WP-C3 | OpenVINO: no infer mutex, memcpy without dtype check, catch-all flattens typed errors | P1 | audit (verified) |
| WP-C4 | TRT: no infer mutex, no cudaSetDevice at infer entry, memcpy without dtype/dims check | P1 | audit (verified; lane not compilable locally — disclosed) |
| WP-C5 | Python worker live-timeout misclassified as crash | P2 | audit (verified) |
| WP-C6 | TensorBlob::fromMat drops channel dim for 2-D multi-channel Mats; isValid() overflow bypass | P2 | audit (verified) |
| WP-D1 | Detection seam rule: center exactly on shared core edge kept by both tiles | P2 | audit (verified) |
| WP-D2 | maxDetections enforced on raw pre-NMS count (post-NMS product cap missing) | P2 | audit (verified) |
| WP-D3 | writeDetectionVector negative classId → OOB read | P2 | audit (verified) |
| WP-E1 | Ledger conservation randomized stress (extension of #1455 fix verification) | guard | prompt WP-03/05 |

## Already-fixed on master (verified, do NOT redo)

* #1455 P1 (corpse-session VRAM ledger): per-instance holder tickets + wrapper
  deleter + regression test `test_model_failure_matrix.cpp:827`. Re-verified in
  code; will re-run test ×2.
* #1459 scratch_registry streaming digest: `streamFnv1a` in `sealDigest` +
  `verifyDigest` (scratch_registry.cpp:173-179, 250-256).
* #1499 master compile break: fixed by #1501 (syntax check exit 0 at f72f0c117).

## Out of track (recorded, read-only)

* #1447 jsoncpp cluster: all 9 sites live in help/faultlab/science_context/
  agent_ops/sdk/cli/preflight — other tracks' ownership.
* #1459 workflow_runtime/workflow_run_coordinator/workflow_checkpoint items: D/E tracks.
* #1505's gpu_plane warm-session eviction: not duplicated.
