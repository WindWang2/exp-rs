# CAPABILITY_MATRIX
See BASELINE.md §3 (authoritative). Status flips are recorded there as milestones land.

## Status updates (as milestones land)

- A. Forward geocoding / RTC from real geometry: **Implemented** (M1) — `rs:sar_geocode`, kernel `sar_geocoding.{h,cpp}`, tests `test_sar_geocoding.cpp`, docs sar-domain.md §4.
- B. Multi-date SAR statistics: **Implemented** (M3) — `rs:sar_temporal_stats`, kernel `sar_temporal.{h,cpp}`, tests `test_sar_temporal_stats.cpp`, docs sar-domain.md §5.
- C. Temporal family: **already satisfied by master** (audit verdict; no second implementation added).
- D. `rs:rasterize` / `rs:zonal_stats`: **Implemented** (M2) — shared seam `rs_raster_vector.{h,cpp}`, tests `test_raster_vector.cpp`, docs raster-vector.md.
- E. Spectral formula drift guard: **Implemented** (M4) — `test_spectral_formula_drift.cpp` (schema enum ↔ table ↔ operator ↔ NaN contracts).
- F/G/H: audit-only verdicts — recorded after adversarial review.

## WP C audit verdict (temporal family) — already satisfied by master

- Mann-Kendall/Sen with tie-corrected variance + continuity correction:
  `temporal/temporal_fit` (`mannKendallSenSlope`), `rs:temporal_sen_trend`;
  seasonal MK + CUSUM + EWMA in `temporal_monitoring`.
- Harmonic fit (optional IRLS Huber), phenology (threshold-fraction +
  integral), breakpoints (greedy RSS, #759 RMSE semantics), decomposition
  (doy climatology + Whittaker trend), anomalies (baseline z-scores),
  gap fill (time-weighted, max_gap_days), composite (quality-weighted
  best-pixel), index series (shared kernels).
- One time-axis contract: `docs/processing/temporal.md` (real day offsets
  for trend/harmonic/breakpoints/phenology/gap-fill; documented index-axis
  limitations for smoothing/decompose). No second implementation added.

## WP F/G/H audit verdicts

- F (terrain/hydrology): fill/D8/accumulation contracts documented and
  tested (terrain_flow.h; test_terrain_foundation5), NoData-as-barrier +
  deterministic tie-breaks + north-up family contract. Distance/morphology/
  focal families present as operators. No verified gap within this track's
  scope; rotated-grid support remains a documented family-wide limitation.
- G (classification): svm/normal_bayes/rf/mlp/knn/min_distance/mahalanobis
  via the RsClassificationPipeline; deterministic seeds documented
  (mt19937(42)); ISODATA tested. Probability/confidence surfaces remain
  OpenCV-dependent — recorded as follow-up, not duplicated here.
- H (contract layer): `scientific_contracts.h` (numeric domain once per
  raster) + nodata-and-statistics.md + temporal.md + sar-domain.md + the
  new raster-vector.md cover the cross-family vocabulary. New 8.0
  operators document against these same contracts (no parallel vocabulary).
