# R14-B Test Results — 2026-10-15

Host: ZCode, worktree /home/kevin/project/exp-rs-r14-b-model-runtime, branch
hardening/r14-B-model-runtime, base f72f0c117. Build: build-r14-B
(Ninja/Release/tests ON). All commands run with QT_QPA_PLATFORM=offscreen,
LD_LIBRARY_PATH=/home/kevin/wp2-prefix/usr/lib:/usr/lib (libodbc for QGIS),
and the repo harness env from build-r14-B/tests/SicnuApplyTestEnv.cmake.

NOTE: `ctest` is not installed on this host — GREEN×2 used the direct
executable invocations with the generated harness environment instead
(equivalent env; no CI substitute claimed).

## RED (production fixes stashed, tests kept)

```
git stash push -- src/operators/runtime/   # tests + CMakeLists kept
cmake --build build-r14-B --target test_r14_B_{detection_decode,runtime_providers,tile_engine}
```
- test_r14_B_detection_decode: exit 42 — 7/7 cases FAILED (14 assertions: NaN
  boxes shipped, batched head accepted, square-auto decode, NMS NaN UB,
  fromMat rank, isValid overflow, negative classId writer).
- test_r14_B_runtime_providers: exit 42 — 4/4 cases, 1 FAILED: "HTTP
  inferNamed did not return within 8s on a dispatcher-less thread" (the
  infinite-hang reproduction on master; the detached watchdog fired).
- test_r14_B_tile_engine: exit 139 (SIGSEGV) — 4/4 cases FAILED; 16126
  assertions red, including the SIGSEGV at the 513-band input
  (CV_CN_MAX wrap → heap corruption) and the strided-head halo misalignment
  (16126 mismatching pixels vs the single-tile reference).

## GREEN ×3 (fixes restored)

| target | run 1 | run 2 | run 3 |
|--------|-------|-------|-------|
| test_r14_B_detection_decode | 279 assertions / 7 cases | same | same |
| test_r14_B_runtime_providers | 15 / 4 | same | same |
| test_r14_B_tile_engine | 20695 / 4 | same | same |

## Regression suite (all against fixed source, after GREEN)

| target | result |
|--------|--------|
| test_model_runtime (ONNX identity + halo seam + LRU + readiness + #1455 corpus) | 100394 assertions / 21 cases PASS |
| test_model_tasks | 12802 / 19 PASS |
| test_model_failure_matrix (incl. #1455 zombie-session VRAM ledger, OOM ladder, cancel, publish fences) | 2158 / 19 PASS |
| test_model_runtime_stress | 68 / 6 PASS |
| test_model_runtime_8 / _9 | 2367/17, 190/16 PASS |
| test_model_ensemble | 112 / 14 PASS |
| test_ensemble_detection | 149 / 17 PASS |
| test_multimodal_inference | 8324 / 8 PASS |
| test_tile_semantic_equivalence | 38 / 3 PASS |
| test_detection_nms_10 | 18 / 5 PASS |
| test_tensor_blob | 173 / 7 PASS |
| test_eo_platform_10 | 578 / 21 PASS |
| test_provider_http | 33 / 5 PASS |
| test_provider_python | 115 / 14 PASS |
| test_gpu_plane | 22 / 4 PASS |
| test_chunk_contract_11 / test_chunk_adoption_11 / test_chunk_resume_11 | 63/10, 435/14, 47/8 PASS |

Total: 21 test binaries, 0 failures.

## Compile-only lanes (no local toolchain — disclosed, not claimed green)

* SICNU_WITH_TENSORRT / SICNU_WITH_OPENVINO lanes: no CUDA/TRT/OpenVINO on
  this host; changes are inside their #ifdefs and cannot compile here.
  TRT ≥10 branch was unbuildable on master too (unguarded legacy APIs), so
  the port cannot regress a compiling lane.
* models/**: 26/26 manifests parse as valid JSON (WP-11); library gate
  covered by the existing test_model_library_manifests suite (not rerun —
  unchanged this branch).
