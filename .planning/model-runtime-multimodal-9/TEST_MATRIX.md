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
