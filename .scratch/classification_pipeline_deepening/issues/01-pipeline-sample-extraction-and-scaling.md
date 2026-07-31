# 01 — RsClassificationPipeline Sample Extraction, Split, and Scaler Integration

**What to build:** `RsClassificationPipeline::Config` is expanded to accept `trainingVector`, `classField`, `maxSamplesPerClass`, `fitScaler`, and `testSplit`. `RsClassificationPipeline::run()` automatically extracts training samples from vector polygons, performs stratified holdout splitting, fits feature scalers, and computes accuracy metrics directly inside the pipeline.

**Blocked by:** None — can start immediately

**Status:** ready-for-agent

- [ ] Add `trainingVector`, `classField`, `maxSamplesPerClass`, `fitScaler`, `testSplit` fields to `RsClassificationPipeline::Config`.
- [ ] Implement vector sample extraction in `RsClassificationPipeline::run()` when `trainingVector` is set.
- [ ] Implement stratified holdout split and `RsFeatureScaler` fitting in `RsClassificationPipeline::run()` when `fitScaler` is enabled.
- [ ] Add `VectorOpenFailed`, `VectorNoLayers`, `ClassFieldNotFound`, `NoValidPixels`, and `InsufficientSamples` to `RsClassificationPipelineResult::Error`.
- [ ] Unit tests in `tests/test_classification_pipeline.cpp` verify vector training extraction, feature scaling, and accuracy assessment.
