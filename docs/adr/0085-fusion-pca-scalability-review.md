# ADR 0085: Fusion/PCA Scalability Review

## Context

The mission's C3 asks to review image-fusion and PCA for alignment
assumptions, NoData behavior, and large-image scalability. Findings from the
review: `processNativeFusion` read both rasters at the pan dimensions with no
co-registration check (mismatched CRS or disjoint extents produced silent
garbage), and `ImageEnhancement::pca` / `computeCovarianceMatrix` included NaN
pixels in the mean and covariance (a single invalid pixel corrupted the whole
decomposition).

## Decision

1. **Fusion grid preflight** (`ImageFusion::processNativeFusion`, ADR 0066
   service): pan and MS rasters are compared with `compareGrids`. Differing
   resolutions are the *point* of pan-sharpening (MS is resampled onto the pan
   grid), so `PixelSizeMismatch` is allowed; every other blocking issue
   (missing CRS, CRS mismatch, origin misalignment, extent mismatch) fails
   with an actionable "not co-registered" error before any pixel work.

2. **PCA NoData handling** (`ImageEnhancement`): the per-band mean excludes
   NaN pixels, and `computeCovarianceMatrix` (shared by PCA and MNF) skips NaN
   pixels per band pair, so invalid pixels no longer corrupt the covariance.

## Consequences

- Fusion between non-overlapping or differently-CRS'd rasters is now a clear
  typed failure instead of silent garbage; differing-resolution pan/MS remain
  supported (the method's purpose), with tests pinning both behaviors.
- PCA/MNF with NoData/NaN pixels produce finite, correct statistics (pinned
  by a NaN-pixel test where explained variance still sums to 1).
- Memory policy is unchanged: fusion and PCA stay honestly labeled FullRaster
  (ADR 0073); this review did not change their O(width×height) allocation.
