# TEST_MATRIX — model-runtime-multimodal-9

Discipline: compiled+executed+passed / compiled NOT executed (marked) /
never compiled (capability gate) — no silent downgrade of claims.
Build: `build-mr9` (Release, Ninja, -j4), ORT SDK = GPU 1.30
(~/.local/opt/onnxruntime-linux-x64-gpu-1.30.0; C-API headers 1.20.1 —
versioned GetApi compatibility). CUDA tests run WITHOUT LD_LIBRARY_PATH
sunless cuDNN is needed; the cuDNN dir is documented in PERFORMANCE.md.

## Regression floor (branch point 132da5e998 + GDAL 3.13 compat fix)

- [x] `sicnu-operators` + all model suites COMPILE against the GPU SDK.
- [ ] Full suite run recorded (after 9.0 changes land — first run covers
      the union; failures get bisected via stash if needed).

## Final recorded sweep (post-merge with origin/master f316dfdbb4, frozen sources, quiet machine)

| Suite | Result |
|---|---|
| test_model_runtime_9 (NEW) | 16 cases / 189 assertions PASS |
| test_model_runtime | 21 cases / 100,394 assertions PASS |
| test_onnxruntime_provider | 11 cases / 2,165 assertions PASS (forced-CUDA refusal case SKIPS with capability WARN — GPU build carries the CUDA EP) |
| test_model_runtime_8 | 14 cases / 1,770 assertions PASS |
| test_model_failure_matrix | 16 cases / 126 assertions PASS |
| test_model_tasks | 9 cases / 1,249 assertions PASS |
| test_tensor_blob | 7 cases / 74 assertions PASS |
| test_device_planner | 8 cases / 95 assertions PASS |
| test_provider_python | 5 cases / 32 assertions PASS |
| test_provider_http | 4 cases / 25 assertions PASS |
| test_model_catalog_v2 | 23 cases / 383 assertions PASS |
| test_model_manifest7 | 14 cases / 62 assertions PASS |
| test_model_library_manifests | 6 cases / 237 assertions PASS |
| **Total** | **154 cases / 114,661 assertions — 0 failures** |
| help_coverage | 8,159 assertions PASS (rs:infer schema additions consistent with help) |
| test_capability_drift | 2 failures PRE-EXISTING on master (harness knowledge coverage of scientific operators; 8.0 recorded the same; not this track) |
| bench (SICNU_MODEL_BENCH=1) | PASS; benchmarks/model-runtime-9-cuda.json written (see PERFORMANCE.md) |

## 9.0 additions

| Suite | Milestone | Scope |
|---|---|---|
| test_model_runtime_9 (NEW) | M0 | schema ⊇ parsed params (rs:infer #872 + task ops), device-token strictness, taxonomy projection incl. Timeout |
| test_model_runtime_9 | M3 | per-feed preprocess known answers (5/30), global-contract invariance, fingerprint determinism/honesty, manifest vocabulary |
| test_model_runtime_9 | M5 | feather blending known answers: hard-edge vs cosine-weighted seam, core invariance |
| test_model_runtime_9 | M4 | dynamic-T NCTHW truth (rank-5, T=3 feed-defined), per-frame provenance, rank-5-output typed refusal |
| test_model_runtime_9 | M7 | aux-file closed vocab, digest verification at resolve, package-digest session identity (cache miss on change), missing-aux readiness |
| test_model_runtime_9 | M8 | verifyProductProvenance typed verdicts: Missing/Malformed/UnsupportedSchema/Ok/expectation-mismatch/GridMismatch/StaleProduct |
| test_onnxruntime_provider | M1 | REAL CUDA EP lane (capability-gated: probe-append then known-answer on GPU; typed refusal on CPU-only ORT hosts) |
| test_provider_python | M1 | worker CUDA_VISIBLE_DEVICES pinning + providers/runtime_version negotiation |
