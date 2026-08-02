# ADR 0055: Move JM Sample Extraction into the Analysis Layer; Consolidate Class-Map Writing + Dtype Policy

## Status
Accepted

## Context
The main window's JM path hand-collected ROI pixels and read each band with
per-pixel 1×1 `GDALRasterIO` — the pattern `RsTrainingDataExtraction::buildMatrices`
was built to eliminate — and skipped the NoData/ignore filtering the classify
path applies, so NoData leaked into JM stats. Separately, the OBIA classify
operator re-implemented the 255/65535 dtype escalation via
`segutil::writeClassGeoTiff`, and the hierarchy operator hand-rolled the
`classField`→`class`→`id` fallback owned by `RsTrainingDataExtraction::classFieldIndex`.

## Decision
1. **`RsJmSeparability::computeAll(X, y)`** consumes `RsTrainingDataExtraction`
   output, splits it into per-class buckets (fewer than 2 samples skipped),
   and returns the full pairwise JM map; the main window's JM path now calls
   `extract()` + `computeAll` — scanline-grouped reads and the same
   NoData/ignore filtering as classify.
2. **`RsPostProcess::saveLabelRaster` becomes the canonical class-map
   writer**: adopts the ADR 0019 S4 three-tier dtype policy (Byte ≤ 255,
   UInt16 ≤ 65535, Int32 beyond), palette only for Byte, optional NoData
   marker (NaN = none); the OBIA classify operator delegates its write.
3. **The hierarchy operator's class-field resolution** now calls `RsTrainingDataExtraction::classFieldIndex`.
4. **Pipeline inline writer left as-is**: tile-streamed predict with crop
   offsets, palette index 0, options-fallback create — not a clean win now.

## Consequences
- JM stats exclude NoData/ignore pixels (previously leaked); overlapping ROIs
  dedup by pixel, last class wins, matching training extraction.
- One canonical dtype policy and one class-field fallback chain; OBIA keeps
  UInt16 for 256..65535 ids (not the old Int32).
- GUI post-process task passes NaN and keeps its no-NoData behavior;
  `segutil::writeClassGeoTiff` remains for ADR 0060 scope.
