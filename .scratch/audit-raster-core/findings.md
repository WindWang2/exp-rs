# Findings Log: Remote Sensing Core Audit

## Baseline Information
- **BASE_SHA**: `19843d1b6910c9207c7e5c97863a873db679368e`
- **Branch**: `agy/audit-raster-core-20260815`

## Confirmed Findings Summary
| ID | Severity | Area | File:Line | Invariant / Defect | Master Status | Proposed Fix |
|---|---|---|---|---|---|---|
| F-001 | P2 | Build/Config | `CMakeLists.txt:23` | `COMPLETE_VERSION` evaluated before `CPACK_PACKAGE_VERSION_*` set, producing `.so...` broken shared library targets | CONFIRMED | Move CPACK definitions before COMPLETE_VERSION |
| F-002 | P0 | Grid / Mask | `src/operators/rs/rs_apply_mask_operator.cpp:74-101, 317-331` | `maskWindowBounds` skips masks contained within a block (only checking 4 block corners), and unbounded indexing into `maskBuf` can cause buffer over-read | CONFIRMED | Compute proper geometric bounding box intersection and bounds-check indices; optimize affine per-pixel inversion |
| F-003 | P1 | NoData Contract | `src/operators/rs/rs_mosaic_operator.cpp:321-326, 348-350` | Unfilled mosaic regions filled with `NaN` while output metadata declares `meta.nodata` (e.g. `-9999`), causing GIS readers to treat empty gaps as valid pixels | CONFIRMED | Initialize `tileOut` with declared output NoData value |
| F-004 | P1 | ML / KMeans | `src/operators/rs/rs_kmeans_operator.cpp:184-235` | NoData / NaN pixels enter KMeans training matrix unvetted, poisoning centroids; reservoir sampling RNG uses platform-dependent `size_t` distribution | CONFIRMED | Filter NoData/NaN via `rsCollectBandNodata` and use deterministic modulo RNG |
| F-005 | P0 | OBIA / Numerics | `src/analysis/segmentation/rs_segment_features.cpp:273, 283` | `(int)NaN` cast in GLCM texture feature calculation causes undefined behavior and invalid stack array writes to `glcm[16][16]` | CONFIRMED | Check `!std::isfinite(val)` before computing GLCM levels |
| F-006 | P3 | Classification | `src/analysis/classification/rs_post_process.cpp:175-186` | Majority filter immediately overwrites center pixel value, systematically biasing ties towards smaller class IDs instead of preserving the center pixel | CONFIRMED | Preserve center pixel value in tie-breaking logic |
| F-007 | P1 | Radiometric | `src/processing/algorithms/radiometric_calibration.cpp:400-430, 517-555` | Calibration scales NoData/NaN pixels into valid radiances/reflectances and omits NoData metadata on output raster | CONFIRMED | Pass NoData to conversion functions, preserve NoData/NaN pixels, and write NoData metadata to output dataset |
| F-008 | P1 | Atmospheric | `src/processing/algorithms/atmospheric_correction.cpp:28-35, 90-98, 401-475` | Dark object stats (DOS1/DOS2) include scaled NoData pixels in histogram, picking NoData background as the dark object; output missing NoData metadata | CONFIRMED | Skip NoData in DarkObjectStats and set output dataset NoData |
| F-009 | P2 | Spectral / Numerics | `src/processing/algorithms/spectral_classification.cpp:63-65, 152-156` | SID (Spectral Information Divergence) silently skips zero-probability terms, falsely assigning divergence 0 (perfect match) to disjoint spectra | CONFIRMED | Set divergence to infinity when one probability is > 0 and the other is 0 |

## Detailed Finding Records

### F-001: Premature COMPLETE_VERSION Definition
- **Severity**: P2
- **Confidence**: 1.0 (reproduced)
- **File:Line**: `CMakeLists.txt:23`
- **Invariant**: `COMPLETE_VERSION` must resolve to valid major.minor.patch version string like `1.0.0`
- **Reproduction**: Inspect `build/src/core/libqgis_core.so*` -> symlinked to `libqgis_core.so...`
- **Root Cause**: `set(COMPLETE_VERSION ...)` called before `CPACK_PACKAGE_VERSION_MAJOR/MINOR/PATCH` were defined at line 151.
- **Master Status**: CONFIRMED
- **Proposed Fix**: Set CPACK version variables before calling `set(COMPLETE_VERSION ...)`.
- **Tests**: Library targets link cleanly as `libqgis_core.so.1.0.0`.

### F-002: Apply Mask Operator Bounding Window & OOB
- **Severity**: P0
- **Confidence**: 1.0
- **File:Line**: `src/operators/rs/rs_apply_mask_operator.cpp:74-101, 317-331`
- **Invariant**: Mask bounding window calculation must correctly detect mask overlap when mask is contained within a block, and mapping must never over-read `maskBuf`.
- **Root Cause**: `maskWindowBounds` only checked the 4 block corners. When mask is inside a block, all corners map outside the mask extent, returning false and leaving the block unmasked.
- **Proposed Fix**: Compute true continuous bounding box intersection in mask space clamped to `[0, maskW-1] x [0, maskH-1]`, verify offsets before accessing `maskBuf`, and hoist affine transformation out of the inner loop.

### F-003: Mosaic Operator NoData Metadata vs Pixel Value Mismatch
- **Severity**: P1
- **Confidence**: 1.0
- **File:Line**: `src/operators/rs/rs_mosaic_operator.cpp:321-326, 348-350`
- **Invariant**: Pixels with no coverage in mosaic must have the exact value declared as the output band's NoData metadata.
- **Root Cause**: `tileOut` initialized to `quiet_NaN()` unconditionally while output metadata was set to `meta.nodata` (e.g. `-9999`).
- **Proposed Fix**: Use `resolvedNoData` to initialize `tileOut`.

### F-004: KMeans NoData / NaN Centroid Poisoning
- **Severity**: P1
- **Confidence**: 1.0
- **File:Line**: `src/operators/rs/rs_kmeans_operator.cpp:184-235`
- **Invariant**: NoData and NaN pixels must not be used as training samples in unsupervised KMeans.
- **Root Cause**: Raw pixel buffers were read and assigned to `reservoir` / `sampleIdx` without checking NoData or `isnan`.
- **Proposed Fix**: Use `rsCollectBandNodata` and skip invalid pixels during sample selection.

### F-005: GLCM Texture NaN Cast Undefined Behavior
- **Severity**: P0
- **Confidence**: 1.0
- **File:Line**: `src/analysis/segmentation/rs_segment_features.cpp:273, 283`
- **Invariant**: `(int)NaN` cast must not occur; non-finite pixels must not index into stack memory `glcm[16][16]`.
- **Root Cause**: `level1` and `level2` conversion did not check `!std::isfinite(val)`.
- **Proposed Fix**: Skip non-finite pixels before level calculation.

### F-006: Majority Filter Tie-Breaker Bias
- **Severity**: P3
- **Confidence**: 1.0
- **File:Line**: `src/analysis/classification/rs_post_process.cpp:175-186`
- **Invariant**: When counts are tied, majority filter must preserve the center pixel's label.
- **Root Cause**: `bestVal` was unconditionally overwritten by the first frequency entry.
- **Proposed Fix**: Explicitly prefer `centerVal` on ties.

### F-007: Radiometric Calibration NoData Preservation & Metadata
- **Severity**: P1
- **Confidence**: 1.0
- **File:Line**: `src/processing/algorithms/radiometric_calibration.cpp:400-430, 517-555`
- **Invariant**: Radiometric calibration must preserve NoData/NaN values and record NoData metadata.
- **Root Cause**: Gain and bias were applied to NoData values, and `outDataset.setBandNoDataValue` was omitted.
- **Proposed Fix**: Propagate input band NoData to output dataset and preserve NoData/NaN values in `toRadiance`, `toToaReflectance`, and `toBrightnessTemperature`.

### F-008: Atmospheric Correction DOS Dark Object NoData Corruption
- **Severity**: P1
- **Confidence**: 1.0
- **File:Line**: `src/processing/algorithms/atmospheric_correction.cpp:28-35, 90-98, 401-475`
- **Invariant**: Dark object histogram must not include NoData pixels.
- **Root Cause**: `DarkObjectStats` only checked `!std::isfinite(v)`, ignoring scaled NoData values.
- **Proposed Fix**: Pass NoData to `DarkObjectStats` and `outDataset`.

### F-009: Spectral Information Divergence Disjoint Spectra Zero Divergence
- **Severity**: P2
- **Confidence**: 1.0
- **File:Line**: `src/processing/algorithms/spectral_classification.cpp:63-65, 152-156`
- **Invariant**: When one probability is > 0 and the other is 0, KL divergence must be $\infty$.
- **Root Cause**: `if (pb > 0.0 && qb > 0.0)` skipped zero-probability terms.
- **Proposed Fix**: Assign $\infty$ when one spectrum has non-zero probability and the other has zero.
