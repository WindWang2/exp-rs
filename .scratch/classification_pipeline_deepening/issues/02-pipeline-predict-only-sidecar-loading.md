# 02 — RsClassificationPipeline Predict-Only Mode & Sidecar Loading

**What to build:** `RsClassificationPipeline::Config` is expanded to accept `modelLoadPath`. When `modelLoadPath` is set, `RsClassificationPipeline::run()` automatically parses `.meta.json` sidecars (with legacy fallback), creates the classifier backend, loads model weights, applies the stored feature scaler, and executes tiled predictions.

**Blocked by:** 01 — RsClassificationPipeline Sample Extraction, Split, and Scaler Integration

**Status:** completed

- [x] Add `modelLoadPath` field to `RsClassificationPipeline::Config`.
- [x] Implement automatic model & sidecar resolution in `RsClassificationPipeline::run()` for predict-only mode.
- [x] Add `ModelOpenFailed` to `RsClassificationPipelineResult::Error`.
- [x] Unit tests in `tests/test_classification_pipeline.cpp` verify model save & load predict-only workflows.
