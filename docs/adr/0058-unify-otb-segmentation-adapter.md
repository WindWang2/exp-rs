# ADR 0058: Delete the App Layer's Duplicate OTB Segmentation Adapter

## Status
Accepted

## Context
`RsObiaSegmentation::runOtb` re-implemented OTB `Segmentation` CLI orchestration in a different dialect than the analysis layer:
`-mode meanshift -out shp labels.tif` (shp discarded by the GUI) vs `RsOtbSegmenter::segment`'s
`-mode raster` with validation — two CLI dialects for one OTB binary.

## Decision
1. **`RsObiaSegmentation::runOtb` delegates** to
   `RsOtbSegmenter::segment(rasterPath, spec, isCanceled)`, mapping
   `RsObiaSegmentationConfig` 1:1 onto `RsLevelSpec` (maxIteration →
   maxIterations). The ~110-line QProcess orchestration is deleted; no spec
   extension was needed.
2. **The app keeps its preferOtb→fallback policy**, now with an explicit
   comment — a deliberate divergence from the hierarchy path's no-fallback
   rule: the OBIA task must complete on machines without OTB.
3. **`-mode meanshift` vector dialect retired** for OBIA paths; raster mode
   produces the label image directly (identical GUI-visible behavior) plus
   stricter validation.

## Consequences
- One OTB CLI dialect for OBIA paths; cancellation plumbed through
  `RsOtbSegmenter` unchanged; tests pin `usedOtb`/fallback semantics, not logs.
- Future candidates, untouched here: `src/operators/otb/otb_segmentation_operator.cpp`
  (already raster dialect) and `src/processing/providers/otb_tools/algorithms/otb_segmentation.cpp`
  (still the retired `-mode meanshift` dialect).
