# Classification Pipeline Deepening Specification

**Status:** Ready for Implementation  
**Date:** 2026-08-01  
**Subsystem:** `src/analysis/classification/`, `src/operators/rs/`  
**ADR Ref:** [ADR 0019: Move Classification CV/Scaler/Split into Pipeline](file:///home/kevin/projects/exp-rs/CONTEXT.md#L47-L50)

---

## Problem Statement

Currently, `rs_supervised_classification_operator.cpp` is 544+ lines long and acts as a shallow, bloated wrapper. It manually unpacks parameters, calls `RsTrainingDataExtraction` to read vector shapefiles, executes `RsClassificationSplit` for holdout splitting, fits `RsFeatureScaler`, parses `.meta.json` sidecars for predict-only models, and handles errors directly. This duplicates training and pre-processing logic outside `Classification Pipeline`, violating the single deep module seam defined in ADR 0019.

---

## Solution

Deepen `RsClassificationPipeline` in `src/analysis/classification` to encapsulate vector sample extraction, stratified holdout splitting, feature scaler fitting, predict-only model/sidecar loading, and structured error reporting directly within `RsClassificationPipeline::run()`. Reduce `RsSupervisedClassificationOperator` to a ~30-line parameter adapter that populates `RsClassificationPipeline::Config` and maps pipeline errors to operator exceptions.

---

## User Stories

1. As a Remote Sensing analyst, I want `Classification Pipeline` to accept vector training polygon paths directly, so that sample extraction, feature scaling, and model training happen in one unified execution step.
2. As an AI Copilot user, I want classification errors (such as missing vector files or invalid class fields) to produce structured error diagnostics, so that failure causes can be reported accurately to the LLM agent.
3. As a developer, I want `RsClassificationPipeline` to automatically load predict-only models and sidecars (`modelLoadPath`), so that callers do not need to duplicate model sidecar parsing code.
4. As a GIS developer, I want `RsSupervisedClassificationOperator` to be a thin JSON adapter, so that training and classification logic cannot drift between GUI tasks, MCP tools, and CLI operators.
5. As a test engineer, I want `RsClassificationPipeline::run()` to be testable end-to-end without GUI widgets or QgsTask handles, so that classification behavior can be verified in fast unit tests.

---

## Implementation Decisions

- Module `RsClassificationPipeline` in `src/analysis/classification/rs_classification_pipeline.h` will be deepened to serve as the single authority for vector sample extraction, data pre-processing, training, model persistence/loading, and tiled prediction.
- Config additions in `RsClassificationPipeline::Config`:
  - `QString trainingVector`: path to vector training shapefile or OGR layer.
  - `QString classField`: integer class ID field name (default `"class_id"`).
  - `int maxSamplesPerClass`: sample count cap per class (default `5000`).
  - `bool fitScaler`: flag to auto-fit `RsFeatureScaler` on training split (default `true`).
  - `double testSplit`: holdout split fraction `0.0-0.9` for accuracy assessment.
  - `QString modelLoadPath`: model file path for predict-only mode (auto-loads `.meta.json` sidecar, scaler, and classifier backend).
- Error Enum Extensions:
  - Add `VectorOpenFailed`, `VectorNoLayers`, `ClassFieldNotFound`, `NoValidPixels`, `InsufficientSamples`, and `ModelOpenFailed` to `RsClassificationPipelineResult::Error`.
- Operator Simplification:
  - Refactor `RsSupervisedClassificationOperator::run()` into a thin adapter (~30 lines) that deserializes JSON inputs into `RsClassificationPipeline::Config` and maps `RsClassificationPipelineResult::Error` to `RSOperatorError`.

---

## Testing Decisions

- Testing seam: `RsClassificationPipeline::run()` in `tests/test_classification_pipeline.cpp`.
- Prior art: `tests/test_classification_pipeline.cpp`, `tests/test_rs_operators.cpp`.
- Test cases:
  1. Train + Predict from vector shapefile: verify sample extraction, model training, feature scaling, and output GeoTIFF pixel values.
  2. Predict-Only with model persistence: train and save model with `modelSavePath`, then load and classify with `modelLoadPath`, asserting identical predictions.
  3. Holdout Accuracy: verify confusion matrix and Kappa coefficient calculations when `testSplit > 0.0`.
  4. Error handling: verify structured errors for invalid vector paths, missing class fields, or missing model files.

---

## Out of Scope

- Unsupervised classification (KMeans) pipeline changes.
- Post-processing operations (sieve, majority filter, clump, recode).
- Modifying Qt GUI classification dock widgets.

---

## Further Notes

- Aligns with ADR 0019 and updated [`CONTEXT.md`](file:///home/kevin/projects/exp-rs/CONTEXT.md#L47-L50).
