# 03 — RsSupervisedClassificationOperator Simplification & Error Code Mapping

**What to build:** Refactor `RsSupervisedClassificationOperator` into a thin ~30-line JSON adapter that passes parameters into `RsClassificationPipeline::Config` and maps `RsClassificationPipelineResult::Error` to `RSOperatorError`. Removes ~500 lines of duplicated extraction/scaling/sidecar parsing code from the operator wrapper.

**Blocked by:** 02 — RsClassificationPipeline Predict-Only Mode & Sidecar Loading

**Status:** ready-for-agent

- [ ] Refactor `RsSupervisedClassificationOperator::run()` into a thin adapter over `RsClassificationPipeline::run()`.
- [ ] Map `RsClassificationPipelineResult::Error` to `RSOperatorError` error codes.
- [ ] Remove duplicated sample extraction, feature scaling, and sidecar loading code from `rs_supervised_classification_operator.cpp`.
- [ ] Unit tests in `tests/test_rs_operators.cpp` and `tests/test_classification_pipeline.cpp` verify operator parity and error handling.
