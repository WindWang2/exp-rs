# TEST_PLAN — R10-A

## Build / test protocol

- Configure: `configure_r10.cmd` (Ninja/Debug, vcpkg manifest cache-hits, Qt 6.8.0 @ C:\deps).
- Build: `build_r10.cmd` (`CMAKE_BUILD_PARALLEL_LEVEL=2`, MSVC, project rule cap; drop to 1 under RAM pressure).
- Tests: `ctest --test-dir build-dev -R <family> -j1` with `QT_QPA_PLATFORM=offscreen`
  (CTestCustom.cmake governs env isolation); targeted families first, then full pass ×2.

## RED phase (tree = master + tests only, commits 9ae8079c6 + b1bde250b)

Expected failures (the fixture IS the defect claim; any fixture that unexpectedly
passes gets its claim re-audited before we keep or drop it):

| fixture (test file) | defect | expected RED signal |
|---|---|---|
| test_pixel_ignore_options.cpp "±Inf is ignored" | AN-1 | isIgnoreScalar(+Inf) returns false |
| test_simple_segmenter.cpp "Inf must not collapse" | AN-5 | segmentCount()==1 (whole-image collapse) |
| test_simple_segmenter.cpp "diagonal-only contact" | AN-6 | region pixelCount < minRegionSize |
| test_segment_features.cpp "Inf excluded from stats" | AN-8 | mean/max Inf, mean != 323/15 |
| test_post_process.cpp "sieve does not swap" | AN-2 | dst(2,2)=2, dst(2,3)=1 (swap) |
| test_classifier_kmeans.cpp "repeated fits agree" | AN-4 | run-to-run center drift |
| test_object_hierarchy.cpp "rasterize failure fails closed" | AN-7 | err empty, silent vote loss |
| test_io_operators.cpp "string bounds form" | PR-1 | stringMinX != arrayMinX (dropped -te) |
| test_io_operators.cpp "gdal:reproject preserves old output" | PR-2 | sentinel file deleted |
| test_model_runtime_8 / test_sar_platform11 (STAC) | PR-9 | Json::LogicError escapes |
| test_feature_cube.cpp version cases | PR-10 | LogicError / silent 1.9→1 |
| test_registration_operators.cpp scenes types | PR-11 | LogicError / silent "5" id |
| test_rs_operators.cpp recode mapping | PR-12 | entry silently dropped |
| test_chunked_operators_parity.cpp mode forms | PR-5 | silent default fallback |
| test_operator_preflight_refusals / test_model_runtime_8 bands | A1-D2 | scalar silently ignored |
| test_fast_6s_atmospheric.cpp non-finite params | AN-9 | all-NaN coefficients |
| test_preflight.cpp WKT dialect + grid | PR-4 | false crs_mismatch / blind resolution |

## GREEN phase

1. Apply batch-1 fixes (main agent): AN-1/2/4/5/6/7/8, PR-1, PR-2, PR-8(io sites).
2. Apply `.planning/r10-algorithms/research/BATCH2_FIXES.patch` (S2-prepared, git-apply checked).
3. Rebuild affected targets; run each new fixture → green.
4. Regression: full `ctest -j1` twice; compare against pristine-master baseline for
   any pre-existing failures (disclose, never mask).

## Out-of-scope execution (must NOT be run/modified)

temporal_workspace.cpp, model_catalog*, rs_json_params.cpp, query_planner.cpp,
radiometric_state*, model_runtime.*, task_center.*, sdk/**, python/** — in-flight PRs #1475/#1476/#1477.
