# TEST_MATRIX
Populated per milestone; each row = behavior → test case → executed evidence (target, command, result).

## Planned/executed matrix (executed evidence appended after local runs)

| Behavior | Test file | Case |
|---|---|---|
| Orbit contract parser accepts valid / refuses every malformed class | test_sar_geocoding.cpp | parseSarSceneContract |
| Forward mapping matches analytic circular-orbit truth (row/col/incidence/factor/class) | test_sar_geocoding.cpp | geocodeGroundCell analytic |
| Backward∘forward round-trip closure on the height shell | test_sar_geocoding.cpp | round-trip (seeded) |
| Tilted facet vs independent vector math (θL, factor, layover/normal classes) | test_sar_geocoding.cpp | tilted facet (real) |
| Sampler exactness / NaN taps / bounds | test_sar_geocoding.cpp | bilinear+nearest |
| Operator E2E: linear radiometry round-trip + counters + provenance metadata | test_sar_geocoding.cpp | e2e round-trip |
| Operator E2E: layover classes + gamma0 RTC from real geometry (nearest) | test_sar_geocoding.cpp | e2e tilted |
| Operator refusals: corrupt orbit, window/grid contradiction, no CRS, rotated grid, missing contract | test_sar_geocoding.cpp | refusals |
| SAR temporal closed forms (mean/std/cv/baseline/deviations/changed dates) | test_sar_temporal_stats.cpp | kernel closed forms |
| Invalid-sample bookkeeping + all-invalid refusal + negative threshold | test_sar_temporal_stats.cpp | kernel edge |
| Operator E2E: constant scenes closed-form bands; declared-dB conversion; robust change count; grid/domain/<2-scene refusals; NaN bookkeeping | test_sar_temporal_stats.cpp | e2e set |
| Rasterize: constant/attribute burn, last-wins, NaN NoData, ALL_TOUCHED | test_raster_vector.cpp | rasterize set |
| Rasterize refusals: non-numeric field, no CRS, rotated grid | test_raster_vector.cpp | refusal set |
| Zonal: exact closed-form stats incl. median, window-spanning accumulation | test_raster_vector.cpp | 512² grid case |
| Zonal: overlap last-wins, sentinel exclusion, empty zone row, geometryless count | test_raster_vector.cpp | overlap set |
| Zonal: CRS84→UTM zone transform | test_raster_vector.cpp | UTM case |
| Zonal refusals: missing zone field, empty vector, no CRS | test_raster_vector.cpp | refusal set |
| Spectral schema enum == drift-table coverage | test_spectral_formula_drift.cpp | coverage |
| Spectral operator == documented formulas (21 indices) | test_spectral_formula_drift.cpp | probe matrix |
| dNBR = NBR(pre) − NBR(post) | test_spectral_formula_drift.cpp | dNBR |
| Degenerate denominator → NaN | test_spectral_formula_drift.cpp | NaN contract |
| Shipped meta sidecars byte-identical to descriptors (incl. 4 new) | test_algorithm_meta_drift.cpp (existing) | catalog |

## Resource bounds used in tests
- Grid sizes ≤ 512×512; vectors ≤ 4 features; no network, no GPU.
- Rasterization/feature caches bounded by constants documented in the headers.
