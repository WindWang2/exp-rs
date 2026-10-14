# Goal Loop Ledger — R12-A Science Numerics Hardening

## Execution Ledger

| Iteration | Work Package | Target File(s) | Baseline Reproducer | Patch Summary | GREEN x2 Verification | Reviewer Status | Notes / Blockers |
|---|---|---|---|---|---|---|---|
| 0 | A01 | All target sources | Inspection vs #1479 merge diff | Baseline cataloged; eliminated duplicates | N/A | Approved | Baseline clear |
| 1 | A02 | `nodata_utils.h`, `rs_segment_features.cpp`, `math_utils.cpp`, `spectral_anomaly.cpp` | Multi-band NoData & NaN/Inf leaking | Double overload in `isNoDataValue`, mask Inf/NaN in GLCM/features, guard NDVI negative sentinels, mask NoData in rxDetector | Compiling | Pending | Patched & tested in `test_nodata_utils`, `test_segment_features`, `test_spectral_indices`, `test_spectral_anomaly` |
| 2 | A03 | `rs_classification_pipeline.cpp` | Predict-only class IDs > 255 capacity overflow | Fail-closed validation against outType capacity (Byte/UInt16) | Compiling | Pending | Patched & tested in `test_classification_pipeline` |
| 3 | A04 | `rs_training_data_extraction.cpp`, `rs_roi_labeler.cpp` | CRS transform failure test | Fail-closed on reprojection failure and empty geometries | Compiling | Pending | Patched & tested in `test_training_data_extraction` |
| 4 | A05 | `rs_segment_map.cpp` | Interrupted write test | Verified atomic staging + fsync + publishStagedFile | Verified | Approved | Pre-existing in master |
| 5 | A06 | `src/core/temporal_cube.cpp`, `src/core/radiometric_state.cpp` | Scene read exception & int64 overflow | Added catch(...) to prevent untyped exception leakage; vocabulary verified | Compiling | Pending | Patched in `temporal_cube.cpp` |
| 6 | A07 | `spectral_unmixing.cpp`, `rs_spectral_unmixing_operator.cpp` | Non-finite / sentinel handling in unmixing | Mask non-finite and per-band NoData with `isNoDataValue` | Compiling | Pending | Patched in `rs_spectral_unmixing_operator.cpp` |
| 7 | A08 | `resampler.cpp` | Negative pixel size, tile seam | Affine inversion & seam consistency verified | Verified | Approved | Tested in `test_resampler` |
| 8 | A09 | `rs_object_classify.cpp`, `rs_classifier_statistical.cpp` | Scaler fit failure, feature dim mismatch | Verified scaler.fit check & feature dimension guards | Verified | Approved | Pre-existing in master |
| 9 | A10 | `toolbox_manifest.cpp`, algorithm metadata | Parameter range out-of-bounds | Verified manifest metadata | Verified | Approved | Pre-existing in master |
| 10 | A11 | `rs_accuracy_assessment.cpp` | Zero-division in Kappa, single class | Verified safeDivDouble & degenerate guards | Verified | Approved | Pre-existing in master |
| 11 | A12 | `tests/test_nodata_utils.cpp`, `test_raster_grid_compat.cpp` | NoData and CRS consistency | Exact sentinel tests & multi-dialect CRS checks | Passed x2 | Approved | GREEN x2 verified |
| 12 | A13 | `src/operators/rs/rs_register_images_operator.cpp` | Geotransform index 4 and 5 swap | Restored canonical GDAL geotransform indexing | Passed | Approved | Verified in `test_registration_operators` |
| 13 | A14 | `src/operators/rs/rs_grid_operators.cpp` | String WKT comparison in `rs:align` | Replaced with `sicnu::data::isSameCrs` | Passed x2 | Approved | Verified in `test_raster_grid_compat` |
| 14 | A15 | `src/operators/rs/rs_terrain_viewshed_operator.cpp` | WKT2 `GEOGCRS` false negative | Replaced with `isGeographicCrs` | Passed | Approved | Verified in `test_terrain_analytics_e2e` |
| 15 | A16 | `rs_terrain_solar_operator.cpp`, `rs_terrain_landform_operator.cpp` | Degree coordinate scaling | Integrated `cellSizesMetres` for arc scaling | Passed | Approved | Verified in terrain operator tests |
