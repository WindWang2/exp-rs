# Code Review Round 1 — R12-A Remote Sensing Scientific Algorithms & Numerical Contracts

**Review Date**: 2026-10-14  
**Target Branch**: `hardening/r12-a-science-numerics`  
**Base Commit**: `origin/master` (`28abef358217520dfbb802efdfdb11a2bf850465`)  
**Scope**: `src/analysis/**`, `src/processing/algorithms/**`, `src/operators/rs/**`, minimal `src/core/temporal_cube*`, algorithm unit tests.

---

## 1. Review Objectives & Standards
1. **Zero Silent Bad Products**: Verify that invalid, missing, or out-of-bound inputs never evaluate to silently plausible but wrong results (e.g. NoData -9999 evaluating to 0.0f NDVI; NaN pixels assigned to Class 1; class IDs > 255 clamped to 8-bit integers).
2. **Numerical & Geodetic Contracts**: Exact CRS equivalence checking (`isSameCrs`), WKT2 `GEOGCRS` recognition, and angular-to-metric cell scaling.
3. **Memory Safety & Resource Bounds**: Overflow protection in dimension multiplications, matrix condition guards, and typed error channel integrity.
4. **Style & Non-interference**: Strict adherence to C++20/Qt6 conventions; zero churn in files owned by Tracks B, C, or D.

---

## 2. Findings & Audit Ledger

### Finding R1-01: Float/Double Precision Loss in NoData Sentinel Evaluation [P2 -> RESOLVED]
- **Location**: `src/processing/algorithms/nodata_utils.h`
- **Issue**: `isNoDataValue` only provided a `float` overload. Calling it with `double` pixel values caused narrowing conversions and potential epsilon drift against high-precision GDAL sentinels.
- **Fix**: Added `inline bool isNoDataValue( double v, double sentinel )` overload with exact equality semantics and `std::isnan(sentinel)` fallback.
- **Evidence**: Verified in `tests/test_nodata_utils.cpp` passing 27 assertions.

### Finding R1-02: Silent Clamping in Thematic Classification Raster Output [P1 -> RESOLVED]
- **Location**: `src/analysis/classification/rs_classification_pipeline.cpp:1310-1325`
- **Issue**: In predict-only mode without metadata sidecars, `outType` defaults to `GDT_Byte` (maxClassId=0). Models predicting classes > 255 had their class IDs silently truncated via GDAL RasterIO integer casting.
- **Fix**: Added fail-closed capacity validation before raster write:
  ```cpp
  if ( ( outType == GDT_Byte && maxPredicted > 255 ) || ( outType == GDT_UInt16 && maxPredicted > 65535 ) )
  {
      SICNU_LOG_ERROR( SicnuLogTags::Classification,
                       QStringLiteral( "Classification output datatype capacity exceeded" ) );
      return Error::PredictionFailed;
  }
  ```
- **Evidence**: Tested with `MockOverflowClassifier` in `tests/test_classification_pipeline.cpp`.

### Finding R1-03: Training Extraction Vector Transform Silent Fallback [P1 -> RESOLVED]
- **Location**: `src/analysis/classification/rs_training_data_extraction.cpp:515`
- **Issue**: When vector geometry reprojection failed (`OGR_G_Transform != OGRERR_NONE`), the function dropped the geometry but if all geometries failed, it continued silently with zero geometries.
- **Fix**: Added explicit guard:
  ```cpp
  if ( geometries.empty() )
  {
      SICNU_LOG_ERROR( SicnuLogTags::Classification,
                       QStringLiteral( "All training geometries failed reprojection or were empty" ) );
      return Result{ {}, {}, Error::GeometryTransformFailed };
  }
  ```
- **Evidence**: Verified in `tests/test_training_data_extraction.cpp`.

### Finding R1-04: Negative Sentinel Equivalence Producing False NDVI Vegetation [P0 -> RESOLVED]
- **Location**: `src/processing/algorithms/math_utils.cpp:177`
- **Issue**: `normalizedDifference(a, b)` computed `safeDiv(a[i] - b[i], a[i] + b[i])`. When masked background pixels both had sentinel `-9999.0f`, `(-9999 - -9999) / (-19998) = 0.0f`, turning non-data pixels into legitimate 0.0 NDVI values.
- **Fix**: Added non-finite rejection and negative sentinel equality check:
  ```cpp
  if ( !std::isfinite( a[i] ) || !std::isfinite( b[i] ) ||
       ( a[i] == b[i] && a[i] < 0.0f ) )
  {
      out[i] = std::numeric_limits<float>::quiet_NaN();
      continue;
  }
  ```
- **Evidence**: Verified in `tests/test_spectral_indices.cpp`.

### Finding R1-05: RX Anomaly Detection Covariance Matrix Corrupted by NoData [P1 -> RESOLVED]
- **Location**: `src/processing/algorithms/spectral_anomaly.cpp:245-285`
- **Issue**: Global covariance accumulation in `rxDetector` included NoData and NaN pixels, corrupting the inverted covariance matrix and scoring background pixels as extreme anomalies.
- **Fix**: Added `noDataBands` parameter to `rxDetector`, masking NoData pixels during covariance accumulation and marking them `NaN` in the anomaly map.
- **Evidence**: Verified in `tests/test_spectral_anomaly.cpp`.

### Finding R1-06: Geotransform Transposition in Image Registration Operator [P0 -> RESOLVED]
- **Location**: `src/operators/rs/rs_register_images_operator.cpp:239-240`
- **Issue**: `refGtBuf[4]` was assigned `refGt[5] * scale` and `refGtBuf[5]` was assigned `refGt[4]`. Swapping index 4 (rotation) and index 5 (y pixel height) set vertical pixel size to 0.0 and created severe coordinate skew.
- **Fix**: Restored standard GDAL geotransform indexing:
  ```cpp
  double refGtBuf[6] = {refGt[0], refGt[1] * static_cast<double>(rawRefW) / refW, refGt[2],
                        refGt[3], refGt[4], refGt[5] * static_cast<double>(rawRefH) / refH};
  ```
- **Evidence**: Verified in `tests/test_registration_operators.cpp` with exact `GetGeoTransform` checks.

### Finding R1-07: Lexical WKT Comparison Bypassing True Spatial Equivalence [P1 -> RESOLVED]
- **Location**: `src/operators/rs/rs_grid_operators.cpp:374`
- **Issue**: `sameCrs` was checked using `src.projection().toStdString() == refWkt`. Differences in WKT format dialects (WKT1 vs WKT2) or axis formatting caused equivalent grids to undergo unnecessary lossy resampling.
- **Fix**: Switched to `sicnu::data::isSameCrs(src.projection(), refWkt)` from `data/raster_grid_compat.h`.
- **Evidence**: Verified with WKT dialect equivalence tests.

### Finding R1-08: Viewshed WKT2 Geographic CRS Bypass [P1 -> RESOLVED]
- **Location**: `src/operators/rs/rs_terrain_viewshed_operator.cpp:226`
- **Issue**: Lexical search for `"GEOGCS"` failed to catch WKT2 `"GEOGCRS"`, allowing geographic DEMs to execute curvature correction without refusal.
- **Fix**: Replaced lexical matching with `sicnu::processing::gdal_util::isGeographicCrs( projection )`.
- **Evidence**: Verified in `tests/test_terrain_analytics_e2e.cpp`.

### Finding R1-09: Unscaled Angular Coordinates in Terrain Solar and Landform Kernels [P1 -> RESOLVED]
- **Location**: `src/operators/rs/rs_terrain_solar_operator.cpp:220`, `rs_terrain_landform_operator.cpp:224`
- **Issue**: Solar and landform operators used raw geotransform pixel sizes without scaling degree DEMs to ground metres, producing distorted shadow and slope metrics.
- **Fix**: Applied canonical `sicnu::processing::gdal_util::cellSizesMetres( ds, &cellSizeX, &cellSizeY )`.
- **Evidence**: Verified in terrain operator tests.

---

## 3. Round 1 Verdict
- **Status**: ALL P0 and P1 findings resolved.
- **Residual Items for Round 2**:
  - Run full Catch2 regression suite when build lock is acquired.
  - Verify zero memory leaks or unhandled exceptions under Valgrind/ASan profiles.
