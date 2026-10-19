# FOLDS_TWIN_NOTES — #1467 buffered-block-folds double implementation (read-only)

Status: NOT consolidated in R10-A. Reasoning below; the consolidation needs the
src/core owner in the loop ( INTERFACE_HANDOFF.md item 4).

## Facts (master 8a0864276)

- `src/analysis/classification/rs_spatial_cross_validation.h:15-24` implements
  `bufferedBlockFolds(nx, ny, bufferDistance)` with the header comment itself
  stating "Isolation invariant mirrors src/core/spatial_split.h:
  ||p_train − p_eval|| > bufferDistance for every retained train/test pair".
- `src/core/spatial_split.{h,cpp}` is the core-side twin. The classification
  side documents parity rather than code reuse.
- Consumption: classification CV (evaluate path, per-fold fit/predict) vs
  core temporal/spatial split consumers; both are tests-only consumers per the
  #1467 note, and `src/processing/CMakeLists.txt:5-9` even compiles the core
  `spatial_split.cpp` into `sicnu_processing`.

## Why not consolidate here

1. Making `qgis_analysis` (or the classification translation unit) include
   `src/core/spatial_split.h` couples the analysis library to the vendored-core
   build target — a build-graph change that #1467 already flags as misplaced
   (the processing target compiles core sources today). Doing a second coupling
   in the opposite direction, from the analysis side, without the core owner,
   risks exactly the "misplaced compilation" churn the issue complains about.
2. Behavior parity is currently asserted by documentation + per-fold audit
   (min retained train↔test distance recorded per fold in the CV audit). A
   divergence would be caught by the audit contract, not silently.

## Recommended follow-up (single coordinated change with the core owner)

- Move `spatial_split.*` consumption to one owner target; then have
  `rs_spatial_cross_validation.h` include it instead of re-implementing, or
  extract the invariant into a tiny shared header both compile against
  (no link dependency).
- Add a property-based test: for random sample clouds, both implementations
  produce identical fold assignments + exclusions (they must, or the doc
  comment "mirrors" is false).
