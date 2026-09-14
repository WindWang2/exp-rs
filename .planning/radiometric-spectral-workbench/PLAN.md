# D13 · Plan — Work Packages A–I, Public Seams & Dependency Order

Pre-agreed public seams (frozen before implementation; tests assert only through these).

## Dependency order (vertical slices)

```
A (state FSM, src/core)            ── used by B preflight, H physics audit
B (calibration + 6S LUT)           ── used by I E2E
C (indices)                        ── used by I E2E, H audit rules
D (continuum removal)              ── used by F context, G annotation, H inspect
E (FCLS + endmembers)              ── used by I E2E
F (spectral library SAM/SRF, src/core) ── used by H library match
G (Qt6 widgets)                    ── standalone GUI surface
H (agent tools)                    ── composes A/C/D/F
I (E2E + lab grading)              ── composes everything
```

## Seam table

| Pkg | File(s) | Namespace / Class | Key signatures (frozen) |
|-----|---------|-------------------|--------------------------|
| A | `src/core/radiometric_state.{h,cpp}` | `exp_radiometric::RadiometricState`, `RadiometricUnit`, `RadiometricStateMismatchException` | `canTransition(unit,unit) noexcept`, `unitToString`, `stringToUnit`, `validateBandPreflight(const QgsRasterLayer*, required)`, `setLayerRadiometricState(QgsRasterLayer*, unit)` |
| B1 | `src/processing/algorithms/radiometric_calibration.{h,cpp}` (append) | `exp_radiometric::RadiometricCalibrator`, `SensorCalibrationParams` | `dnToRadiance`, `dnToToaReflectance`, `radianceToBrightnessTemperature` (all buffer kernels, NoData `-9999.0f`) |
| B2 | `src/analysis/atmospheric/fast_6s_lookup.{h,cpp}` (new) | `exp_radiometric::Fast6sLookup`, `Atmosphere6sParams`, `Lut6sEntry` | `interpolateCoefficients(wlNm, params)`, `invertBoaReflectance(toa→boa, lut)`, `executeDos2(radiance→boa)` |
| C | `src/processing/algorithms/spectral_indices.{h,cpp}` (append) | `exp_spectral::SpectralIndices` | `ndvi/evi/savi/mndwi/ndbi/evi2` with NoData + scale guard (`isScaled`), NaN on \|denominator\| < 1e-7 |
| D | `src/analysis/hyperspectral/continuum_removal.{h,cpp}` (new) | `exp_spectral::ContinuumRemoval`, `SpectralAbsorptionFeature` | `compute(wl, refl, n → continuum, normalized)`, `extractFeatures(wl, normalized, n, minDepth)` — Monotone Chain hull, FWHM by linear half-depth interpolation |
| E | `src/processing/algorithms/spectral_unmixing.{h,cpp}` (append) | `exp_spectral::SpectralUnmixing`, `UnmixingResult`, `EndmemberExtractionMethod` | `extractEndmembers(pixels,…,PPI/VCA,seed)`, `unmixFcls(pixels,…,result,err)` — ANC f≥0 + ASC Σf=1 (penalty-augmented Lawson–Hanson NNLS) |
| F | `src/core/spectral_library.{h,cpp}` (new) | `exp_spectral::SpectralLibrary`, `SpectralLibraryEntry`, `SpectralMatchCandidate` | `fromJson/toJson/loadFromFile/saveToFile`, `matchSpectrum(query, bands, topK, maxAngleRad)` SAM ∈ [0, π/2], `resampleToSensor(Gaussian SRF, ±3σ window)` |
| G | `src/app/widgets/spectral_profile_widget.{h,cpp}` (append), `band_composite_palette.{h,cpp}` (new) | `exp_gui::SpectralProfileWidget`, `exp_gui::BandCompositePalette` | `setProfile/setSpectrum/clear/hasData/currentValues`, signals `featureSelected(wl,depth)` / `inspectionPointChanged(pt)`, `QPointer` guard + auto-clear on layer destroy; palette `bindRasterLayer/setRgbMapping` |
| H | `src/agent/spatial_tools/spectral_spatial_tools.{h,cpp}` (new) | `exp_agent::SpectralInspectTool`, `ValidateBoaPhysicsTool` (extend `sicnu::agent::spatial_tools::SpatialTool`) | `spatial:spectral_inspect`, `spatial:validate_boa_physics`; error codes `INVERTED_WATER_SPECTRUM`, `UNPHYSICAL_REFLECTANCE_RANGE`, `INVERTED_VEGETATION_RATIO` |
| I | `tests/test_d13_radiometric_spectral_e2e.cpp`, `data/labs/lab02…lab.json`, `data/labs/lab08…lab.json` | — | Full chain DN→…→material report; Lab02/Lab08 rubric scores == 100 on reference artifacts |

## CMake registration plan

- `src/core/CMakeLists.txt` (qgis_core sources): add `radiometric_state.cpp/.h`, `spectral_library.cpp/.h`.
- `src/analysis/CMakeLists.txt`: new `sicnu_spectral_analysis` STATIC lib (C++20): `atmospheric/fast_6s_lookup.cpp`, `hyperspectral/continuum_removal.cpp`; links nothing beyond Qt6::Core (headers std-only).
- `src/processing/CMakeLists.txt` (sicnu_processing): already compiles the three appended algorithm files — no registration change.
- `src/app/CMakeLists.txt` (sicnu_geo_rs): add `widgets/band_composite_palette.cpp`.
- `src/agent/CMakeLists.txt` (sicnu_agent): add `spatial_tools/spectral_spatial_tools.cpp`.
- `tests/CMakeLists.txt`: append D13 registrations — `sicnu_add_test()` for kernel tests; widget tests follow the `test_histogram_widget` pattern (compile widget .cpps into the test, AUTOMOC ON).

## Test → truth matrix (anti-tautology)

| Test | Independent truth source |
|------|--------------------------|
| Planck BT | USGS Landsat 8 handbook: L=10, K1=774.8853, K2=1321.0789 → 302.79274 K |
| TOA refl | OLI B4: (0.00002·25000−0.1)/sin(45°) = 0.56568542 |
| 6S inversion | closed-form arithmetic: 0.20/0.852 = 0.23474178 |
| Indices | vegetation endmember: NDVI=5/7, EVI=50/73, SAVI=0.625 |
| Continuum | synthetic Gaussian: D=0.60, FWHM=2√(2ln2)·20=47.0964 nm, λ0=670 |
| FCLS | y = 0.5·m1+0.3·m2+0.2·m3 → f=[0.50,0.30,0.20], Σf=1 |
| SAM | orthogonal geometry: t=[1,1,0], r=[1,0,0] → θ=π/4 |
| SRF resample | constant spectrum invariance: R≡0.35 → 0.35±1e-6 |

## Slice rhythm per package

1. Public seam header + failing Catch2 test (tracer bullet) → red.
2. Minimal implementation → green (`ctest -R <test> -j1`).
3. Boundary/NoData assertions → green, atomic `git commit`.

Refactoring deferred to Phase 3 (two-axis review).
