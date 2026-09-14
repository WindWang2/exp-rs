# PERFORMANCE — eo-ai-model-runtime-foundation-10

## NMS boundedness (F-OPS-5 fix), host under concurrent-track load (~9-16)

Harness: /tmp stand-alone link against build-dev/libsicnu_operators (Debug, -O0),
steady_clock wall time, single run (evidence, not a gate):

- 100,000 mutually disjoint boxes dedup (the F-OPS-5 reproduction shape; the
  dense pass needs ~5e9 IoU compares): **304 ms**, kept=100000.
- 50,000-box dense cluster NMS (heavy-overlap worst shape): **105 ms**, kept=630.

The old dense implementation was O(n²) ≈ minutes at the max_detections budget
(100k); the grid pass is bounded by neighborhood density and honours the cancel
predicate mid-pass (typed Cancelled, verified by test_detection_nms_10).

## 1e5 logical-extent scale evidence (test_eo_platform_10)

- 100000x100000 px sparse GTiff: planning is O(grid) (9409 tiles at 1024 px),
  windowed reads stay O(tile); the run is cancelled mid-way via the operator
  cancel predicate and aborts typed, leaving no torn product and no stage
  leftover (asserted in the test).

## Build/test evidence summary (local only; no online CI)

- test_detection_nms_10: 12 assertions / 4 cases — green.
- test_eo_platform_10: 570 assertions / 21 cases — green.
- Regressions green: test_model_runtime (100394), _8 (1770), _9 (190),
  test_model_tasks (1249), test_multimodal_inference (8324),
  test_model_failure_matrix (126), test_model_catalog_v2 (383),
  test_model_manifest7 (62), test_model_library_manifests (237),
  test_algorithm_meta_drift (2631), test_capability_knowledge (1052).
- test_capability_drift: 19/21 — the 2 remaining failures are byte-identical
  to master @ 7d78059d (harness-track, pre-existing; OUT_OF_SCOPE here).
