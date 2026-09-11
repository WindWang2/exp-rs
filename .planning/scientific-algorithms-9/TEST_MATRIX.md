# TEST MATRIX — Scientific Algorithms 9.0

Environment: clang 22.1.8, Debug, ccache, GDAL 3.13.3, Qt 6, offscreen.
Invocation: `cmake --build build --target <t> -j4` then run the binary with
`QT_QPA_PLATFORM=offscreen` from `build/`.

## New suites (this track)

| Suite | Covers | Old-code-fails evidence | Status |
|---|---|---|---|
| `test_scientific_defects_9` | #848 (2 cases), #853 (2), #854 (1 E2E), #855 (kernel + E2E closed form), #856 (E2E EVI), #873 (contract) | pit stays 2 / labels UB path / NaN tag on mask / averaged-spacing gamma0 / DN-flip EVI ≈ 0 / divisor +Inf | executed |
| `test_semantic_drift_9` | M1 drift guards (6 anchors) | anchors absent on baseline code | executed |

## Existing suites re-run (affected by M0 fixes)

| Suite | Why | Result |
|---|---|---|
| `test_terrain_foundation5` | fillDepressions boundary semantics | All tests passed (259 assertions, 12 cases) |
| `test_adversarial_m1` | endorheic-basin semantics updated with #848 (documented in test) | All tests passed (413 assertions, 13 cases) |
| `test_sar_kernels` | slopeAspectAt signature (isotropic values pinned) | All tests passed (89 assertions, 12 cases) |
| `test_scientific_contracts` | domainFromDeclaredScale finite rule + provenance | All tests passed (42 assertions, 5 cases) |
| `test_e2e_open_issues` | fillDepressions / scale-probe / flatten E2E | All tests passed (216 assertions, 47 cases) |
| `test_spectral_formula_drift` | #856 probe path | All tests passed (145 assertions, 4 cases) |
| `test_sar_operators` | flatten/correction operator paths | All tests passed (530 assertions, 16 cases) |
| `test_sar_geocoding` | new over-budget-fallback known-answer case | All tests passed (5768 assertions, 8 cases) |
| `test_scientific_defects_9` | M0 corpus (after provenance remediation) | All tests passed (116 assertions, 9 cases) |
| `test_semantic_drift_9` | M1 drift guards | All tests passed (23 assertions, 6 cases) |

## M3/M5/M7 audit suites (no code changes — execution evidence)

| Suite | Area | Result |
|---|---|---|
| `test_radiometric_calibration` | M3 optical DN→radiance/TOA/BT | All tests passed (195688 assertions, 27 cases) |
| `test_atmospheric` | M3 DOS correction family | All tests passed (37486 assertions, 40 cases) |
| `test_qa_mask` | M3 cloud/QA mask parsing | All tests passed (97 assertions, 7 cases) |
| `test_temporal_fit` | M5 MK/Sen/harmonic/breakpoints | All tests passed (162 assertions, 23 cases) |
| `test_temporal_core` | M5 time axis, gap-fill, composite | All tests passed (367 assertions, 6 cases) |
| `test_accuracy_assessment` | M7 confusion matrix / kappa / OA | All tests passed (15 assertions, 3 cases) |

Tolerances note (Reviewer B): gamma0 E2E margin 1e-3 covers float32 DEM /
geotransform serialization round-trip; EVI margin 1e-4 on 0.6667 covers the
float32 raster I/O round-trip; both verified against double closed forms.

## Not runnable here / not applicable

- Sanitizer build (`-fsanitize=address,undefined`) planned as a targeted
  compile of `terrain_flow.cpp` + a driver (M9 evidence) — the full
  sanitizer preset rebuilds the world and is scheduled after the feature
  milestones; the UB-safety contract is additionally pinned by the
  #853 behavior tests above.
- Windows/QGIS-standalone packaging: out of scope for this track.
