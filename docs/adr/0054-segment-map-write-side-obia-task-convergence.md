# ADR 0054: Give RsSegmentMap a Write Side; Re-point RsObiaTask onto paint + classify

## Status
Accepted

## Context
`RsSegmentMap` was a read-only label-image model, so writers hand-rolled GDAL
code: the hierarchy operator's `writeLabelGeoTiff` and `RsObiaTask::writeOutput`
— 178 lines duplicating `RsClassRaster::paint` (dtype escalation, palette, row
loop) without its incomplete-output cleanup. `run()` steps 3–5 also duplicated
`RsObjectClassify::classify`'s train-row selection, fit-if-needed, predict.

## Decision
1. **Add `RsSegmentMap::toGeoTIFF(path, refPath, error)`**: UInt32, LZW,
   geotransform/projection copied from the reference, NoData=0, fail-closed —
   missing or size-mismatched reference fails, partial output removed.
2. **Delete the operator's `writeLabelGeoTiff`**; call `toGeoTIFF` (outputs
   gain NoData=0 metadata; otherwise identical semantics).
3. **Re-point `RsObiaTask`**: `writeOutput` delegates to `RsClassRaster::paint`
   (dtype keyed on class ids only, not color keys); run() steps 3–5 become one
   `RsObjectClassify::classify` call. The accuracy-assessment block stays.

## Consequences
- **One label writer** for segment maps; one paint path for class rasters.
- **Cleanup on failure**: partial outputs removed instead of leaked.
- **Dtype escalation pinned to class ids** — a high color-table key no longer
  forces UInt16; reference grid-size mismatch fails instead of out-of-bounds.
- "No labeled segments" error text and the accuracy result preserved (tests
  pin them); other failure messages come from the deep modules.
- Segutil writer (0060), OTB adapter (0058), ROI majority (0060) untouched.
