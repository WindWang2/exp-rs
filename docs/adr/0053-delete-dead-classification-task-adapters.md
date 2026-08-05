# ADR 0053: Delete Dead Classification Task Adapters

## Status
Accepted

## Context
`RsClassificationTask` (app layer) carried a 15-field `Config` ~95% identical
to `RsClassificationPipeline::Config` (only `algoName` vs `methodName`
differs) and hand-copied every field into the pipeline config inside `run()`.
The main window hand-mapped the same fields a second and third time in its
apply and preview `submitJob` lambdas. `RsCvTask`, a QgsTask wrapper in the
headless `src/analysis/classification` directory, was the only GUI-coupled
file there, and `RsClassificationPipeline::runCrossValidation` was a 14-line
pass-through to `RsCrossValidation::kFold` behind a fixed-fraction progress
bridge. All three were production-dead: the window submits jobs directly, and
only tests constructed them.

## Decision
1. **Delete `RsClassificationTask`** (`rs_classification_task.{h,cpp}`). The
   main window builds `RsClassificationPipeline::Config` directly at both the
   apply and preview call sites (`algoName` renamed `methodName`); the
   duplicated field-mapping blocks are removed. The e2e test migrates to the
   pipeline seam, the sanctioned test surface per ADR 0019 decision 5.

2. **Delete `RsCvTask`** (`rs_cv_task.{h,cpp}`). The task-center tests,
   which exercise submit/cancel integration rather than the wrapper itself,
   now subclass `QgsTask` locally in the test file.

3. **Delete `RsClassificationPipeline::runCrossValidation`**. The main
   window's cross-validation job calls `RsCrossValidation::kFold` directly,
   porting the wrapper's fixed-fraction (0.5) progress bridge into its
   existing cancel lambda, preserving the observable progress UX.

## Consequences
- **One config vocabulary**: GUI and pipeline share
  `RsClassificationPipeline::Config`; no more field-name drift.
- **Analysis directory is GUI-free again**; `qgis_analysis` has no QgsTask
  dependencies.
- **Tests use the sanctioned seam** (pipeline and local QgsTask stubs), so
  config duplication cannot silently re-diverge.
- **Cross-validation behavior unchanged**: same `kFold` call, same 50%
  fixed-fraction progress report, same cancellation semantics.
