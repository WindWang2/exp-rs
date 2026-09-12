# CAPABILITY MATRIX — 9.0 track

State legend: Implemented (8.0/earlier, merged) / Gap→Fixed (this track) /
Audit (verified, no code change justified) / Refused (documented non-goal).

| Capability | State | Evidence |
|---|---|---|
| Priority-flood fill on NoData-bordered rasters | Gap→Fixed (M0/M6) | issue #848; `test_terrain_flow_nodata.cpp` |
| UB-free D8 direction decoding | Gap→Fixed (M0/M6) | issue #853; range-guard + regression |
| Per-band NoData on typed multi-band SAR outputs | Gap→Fixed (M0/M2) | issue #854; `test_sar_flatten_nodata.cpp` |
| Anisotropic Horn slope/aspect (SAR terrain) | Gap→Fixed (M0/M2) | issue #855; analytic tilted-plane test |
| Positive-only numeric-domain probing | Gap→Fixed (M0/M4) | issue #856; `test_spectral_scale_probe` |
| Finite validated declared scale | Gap→Fixed (M0/M1) | issue #873; contract unit test |
| Mechanical semantic-drift guard for owned kernels | Gap→Fixed (M1) | `test_semantic_drift_9.cpp` |
| Orbit / geocode / RTC (real LOS) | Implemented (7.0/8.0) | `sar_orbit`, `rs:sar_geocode`, `test_sar_geocoding` |
| SAR calibration σ0/β0/γ0 | Implemented (8.0) | `sar_calibration`, docs sar-domain.md — audited M2 |
| Speckle filters / texture / dualpol | Implemented (8.0) | `sar_speckle`, `sar_texture`, `sar_dualpol` — audited M2 |
| Temporal family (trend/harmonic/phenology/breakpoints) | Implemented (8.0) — audited M5, no duplication | `temporal/*`, docs temporal.md |
| Optical DN→radiance/TOA, QA masks, DOS | Implemented (8.0) — audited M3 | `radiometric_calibration`, `qa_mask`, `atmospheric_correction` |
| Spectral indices + drift guard | Implemented (8.0) — extended M4 | `test_spectral_formula_drift` |
| Terrain slope/aspect/hillshade anisotropic | Implemented (8.0) — audited M6 | `terrain_analysis` cellSizeX/Y |
| Zonal stats / rasterize | Implemented (8.0) | `rs:zonal_stats`, `rs:rasterize` |
| Classification + accuracy | Implemented (8.0) — audited M7 | `test_accuracy_assessment` |
| InSAR coherence | Refused | no SLC/complex input contract (7.0 refusal stands) |
| Quad-pol RVI | Refused | dual-pol approximation honesty rule |

## Audit verdicts (M3/M5/M7 — execution evidence, no code changes)

- **M3 optical**: DN→radiance/TOA/brightness-temperature, QA mask parsing and
  the DOS family carry documented coefficient contracts and executed suites
  (195688 + 37486 + 97 assertions). No verified gap inside this track's
  scope; "physically-level atmospheric correction" remains honestly refused
  (documented in the operator metadata).
- **M5 temporal**: the 8.0 audit verdict stands — one time-axis contract, MK/
  Sen/harmonic/breakpoints/gap-fill/composite executed green on this branch
  (162 + 367 assertions). No second implementation added.
- **M7 classification**: confusion-matrix/kappa/OA suite executed green (15
  assertions); deterministic seeds and pipeline contracts unchanged. The
  8.0-documented follow-up (probability/confidence surfaces,
  OpenCV-dependent) remains out of this track's scope and is carried in the
  PR description as a follow-up, not silently dropped.
