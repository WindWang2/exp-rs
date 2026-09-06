# Processing: NoData, Valid-Observation & Statistics Policy

> Authority for how missing data and streaming statistics behave across the
> `rs:` operator family and their kernels. Companion:
> [validation-policy.md](validation-policy.md),
> [grid-and-radiometric-policy.md](grid-and-radiometric-policy.md).

## 1. Missing-value representation

1. **In memory, missing = NaN.** Kernels operate on `float` buffers where NaN
   means "no observation". Declared GDAL NoData sentinels and non-finite values
   are normalized to NaN at read time (e.g. `TemporalTileReader`,
   `GdalDatasetWrapper::readBandMasked`).
2. **Declared sentinel resolution is centralized** in
   `src/processing/algorithms/nodata_utils.h`:
   - `bandNoDataSentinel(ds, band)` → the declared sentinel cast to float, NaN
     when the band declares no finite sentinel.
   - `isNoDataValue(v, sentinel)` → NaN is always invalid; a declared sentinel
     matches by **exact float-cast equality**. Epsilon comparisons are
     forbidden (they silently drop legitimate values near the sentinel).
3. **Outputs are NaN-NoData** for continuous results: float outputs are written
   with `SetNoDataValue(NaN)` so undefined pixels travel as NoData, never as a
   plausible value. Class/mask rasters use the family's documented encoding
   (e.g. `rs:threshold_raster` writes 1/0 with 255 = NoData; OBIA labels use 0
   = NoData).

## 2. Denominators and valid-observation semantics

Every statistic declares what its denominator counts. The rule:

> **A denominator counts observations that contributed to the numerator.**

- Temporal statistics (summary, composite, trend, harmonic, breakpoints,
  anomaly, extract) accumulate over finite samples only; reported counts
  (`valid_count`, `n`) are the finite-observation counts, never index spans
  (issue #759 is the canonical regression).
- A statistic over **zero valid observations is undefined**: it is reported as
  NaN (or the operator refuses with a typed error where a minimum observation
  count is part of the contract, e.g. `rs:temporal_sen_trend` requires ≥ 3
  scenes, Mann-Kendall needs ≥ 3 valid observations). Reporting `0` for a
  statistic with no data is a bug.

## 3. Variance denominators (sample vs population)

Both conventions exist by design; the contract is which one, where:

| Convention | Denominator | Used by |
|---|---|---|
| **Population** (÷N) | descriptive spread of the observed samples | `MathUtils::computeStats*`, streaming magnitude stats (`StreamingMagnitudeStats`), temporal summary stddev |
| **Sample** (÷N−1) | unbiased estimate for inference/uncertainty | RX anomaly covariance (`spectral_anomaly.cpp`), regression RMSE with df correction (`OnlineRegression::rmse` ÷(N−2)), feature normalization scaler |

Rules:

- A new statistic must state its convention in the header comment next to the
  accumulator and in the operator schema description if user-visible.
- Accumulators reuse `temporal::stats::WelfordAccumulator` /
  `OnlineRegression` (numerically stable, NaN-exclusion is caller-side by
  contract — callers must only `add()` finite samples).
- Do not "fix" one convention to the other without a migration: existing
  declared grades and sidecar metadata document the current values.

## 4. Weighting semantics

- Unless an operator's schema declares otherwise, all valid observations weigh
  equally (binary validity). Acquisition-interval weighting is applied only
  where the time axis is a regressor (trend/harmonic/breakpoints/phenology/
  gap-fill use real day offsets).
- Documented limitation: Savitzky–Golay / Whittaker smoothing and the
  decomposition trend step treat samples as uniformly spaced (index axis).
  For irregular series, gap-fill or harmonic methods are the honest choice —
  the smooth operator's schema points this out.
- Quality weighting (e.g. per-scene QA scores) is only consumed by
  `rs:temporal_composite` best-pixel selection today.

## 5. Enforcement

The policy is pinned by (non-exhaustive):

- `tests/test_temporal_fit.cpp` — #759 RMSE denominator, Sen NaN contracts.
- `tests/test_nodata_utils.cpp` — sentinel resolution, exact-match policy.
- `tests/test_temporal_core.cpp` — reader validity contract (declared NoData,
  non-finite → NaN).
- `tests/test_change_detection.cpp` — threshold valid-observation counting.
