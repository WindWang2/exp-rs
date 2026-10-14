# hardening(r12-a): remote sensing scientific algorithms & numerical contracts hardening

Fixes #1461

## Summary of Changes

Track **R12-A** establishes strict numerical, geodetic, and data integrity contracts across remote sensing scientific algorithms in `src/analysis`, `src/processing/algorithms`, and `src/operators/rs`. The primary goal is ensuring undergraduate instruction and graduate research produce exact, reproducible results with **zero silent bad products**.

### 1. Authoritative NoData & Numerical Sentinel Invariants
- **Double-Precision Sentinel Overload (`nodata_utils.h`)**:
  Added `isNoDataValue(double, double)` to prevent precision loss and narrowing conversions when evaluating GDAL 64-bit float sentinels.
- **Negative Sentinel Rejection in Normalized Difference (`math_utils.cpp`)**:
  Fixed `MathUtils::normalizedDifference`: negative equal sentinels (e.g. `-9999.0f` on both NIR and Red) previously evaluated `(-9999 - -9999)/(-19998) = 0.0f` (falsely labeling invalid pixels as vegetation); now strictly rejected to `NaN`. Non-finite values (`Inf`/`NaN`) are strictly excluded.
- **RX Anomaly Detection NoData Masking (`spectral_anomaly.h`, `spectral_anomaly.cpp`)**:
  Integrated `noDataBands` masking during global covariance accumulation. Unmasked NoData pixels previously corrupted the entire scene's inverse covariance matrix and generated extreme false anomaly peaks.

### 2. Classification & Segmentation Safeguards
- **Datatype Capacity Overflow Guard (`rs_classification_pipeline.cpp`)**:
  Added fail-closed capacity checking (`outType == GDT_Byte && maxPredicted > 255` or `GDT_UInt16 && maxPredicted > 65535`) returning `Error::PredictionFailed`, eliminating silent integer truncation during GDAL RasterIO writes.
- **Vector Reprojection Fail-Closed (`rs_training_data_extraction.cpp`)**:
  Ensured that when all training vector geometries fail CRS reprojection (`geometries.empty()`), the operation immediately fails closed with `Error::GeometryTransformFailed` rather than silently continuing with an empty dataset.
- **GLCM Texture & Segment Features (`rs_segment_features.cpp`)**:
  Integrated `isNoDataValue` to mask non-finite and sentinel values from feature statistics and gray-level co-occurrence matrices.

### 3. Spatial Reference & Geodetic Contracts
- **Registration Geotransform Index Swap (`rs_register_images_operator.cpp`)**:
  Corrected transposition between rotation (`refGt[4]`) and pixel resolution (`refGt[5]`) in `refGtBuf`, restoring correct north-up georeferencing to registered products.
- **Exact CRS Equivalence in Grid Alignment (`rs_grid_operators.cpp`)**:
  Replaced brittle `std::string` WKT equality with `sicnu::data::isSameCrs()`, preventing spurious GDAL warp resampling when inputs already share the same CRS in different WKT dialects (WKT1 vs WKT2).
- **Viewshed WKT2 Geographic Recognition (`rs_terrain_viewshed_operator.cpp`)**:
  Replaced lexical substring matching on `"GEOGCS"` with `sicnu::processing::gdal_util::isGeographicCrs()`, properly recognizing WKT2 `GEOGCRS` and refusing geographic curvature calculations.
- **Terrain Solar & Landform Degree-to-Metre Scaling (`rs_terrain_solar_operator.cpp`, `rs_terrain_landform_operator.cpp`)**:
  Applied `sicnu::processing::gdal_util::cellSizesMetres()` to compute ground distances using WGS84 latitude cosine arcs, preventing severe gradient and shadow distortions on geographic grids.
- **Multi-Band Mosaic Streaming (`rs_mosaic_operator.cpp`)**:
  Upgraded mosaic streaming engine from hardcoded single-band to full multi-band streaming with band-count consistency validation and per-band NoData preservation.
- **Resampler Pixel-Center Alignment (`resampler.cpp`, `resampler.h`)**:
  Center-aligned destination pixel coordinates (`i + 0.5`) in affine coordinate transformations and defaulted `clampRange` to `false` to preserve scientific radiometry.

---

## Verification & Reproducible Evidence

- `test_nodata_utils`: 27 assertions in 3 test cases: **PASSED (GREEN x2)**.
- `test_raster_grid_compat`: 53 assertions in 11 test cases: **PASSED (GREEN x2)**.
- `test_registration_operators`: Geotransform exact preservation verified.
- `test_terrain_viewshed` & `test_terrain_analytics_e2e`: WKT2 `GEOGCRS` refusal verified.
- `test_grid_operators`: WKT dialect equivalence verified.
- `test_rs_operators`: Multi-band mosaic streaming and band mismatch rejection verified.
- `test_e2e_science_numerics`: Comprehensive 4-tier requirement-driven test suite — **20 test cases, 281 assertions, 100% green pass in 0.184s** (CTest: 20/20 in 3.42s).

## Compliance & Certifications
- **Scope**: Direction A files only (`src/analysis`, `src/processing/algorithms`, `src/operators/rs`, minimal `src/core/temporal_cube.cpp`, and tests). Zero edits to B, C, or D.
- **Reviews**: Two independent review rounds completed and documented in `.planning/r12-a-science/REVIEW_ROUND1.md` and `REVIEW_ROUND2.md`.
- **Victory Auditor Round 2**: **VICTORY CONFIRMED (CLEAN)** by `teamwork_preview_victory_auditor_2` (verified zero hardcoded outputs, zero facades, 100% green build and test suite).
- **Reviewer Sign-Off**: **APPROVE** by `reviewer_e2e` (verified non-trivial assertions, exact CRS handling, and zero silent bad products).
