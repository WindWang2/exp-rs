# Test Plan — R12-A Science Numerics Hardening

## Target Verification Test Suites
- `test_multires_segmenter`: Catch2 test covering multi-band NoData per-band voiding.
- `test_classification_pipeline`: Catch2 test covering predict-only class ID datatypes (Byte, UInt16, Int32).
- `test_training_data_extraction`: Catch2 test covering CRS transformation failure rejection.
- `test_segment_map`: Catch2 test verifying atomic GeoTIFF generation.
- `test_temporal_cube`: Catch2 test verifying error attribution and int64 index overflow.
- `test_radiometric_state`: Catch2 test verifying SAR radiometric vocab (`gamma0`).
- `test_classifier_statistical`: Catch2 test verifying feature dimension mismatch checks.
- `test_accuracy_assessment`: Catch2 test verifying zero-division in Kappa and invalid sample handling.

## Execution Rules
- Compilations executed under: `flock /tmp/exp-rs-build-j2.lock -c 'cmake --build build-r12-a-science --parallel 2'`
- Tests run sequentially: `ctest --test-dir build-r12-a-science -j1 --output-on-failure`
- Each fixed work package requires 2 consecutive GREEN runs.
