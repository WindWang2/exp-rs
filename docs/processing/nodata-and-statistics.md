# Processing: NoData, Valid-Observation & Statistics Policy

> Authority for how missing data and streaming statistics behave across the
> `rs:` operator family and their kernels. Companion:
> [validation-policy.md](validation-policy.md),
> [grid-and-radiometric-policy.md](grid-and-radiometric-policy.md).

## 1. Missing-value representation

1. **In memory, missing = NaN.** Kernels operate on `float` buffers where NaN
   means "no observation". Declared GDAL NoData sentinels and non-finite values
   are normalized to NaN where data is read — e.g.
   `TemporalTileReader::normalizeAndMask` for the temporal family, and the
   per-block reads of the streaming operators that inline the same
   normalization (`GdalDatasetWrapper::readBandMasked` is the reference
   implementation of that normalization, exercised by tests; production paths
   currently re-inline it rather than calling it).
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
4. **Typed multi-band outputs declare NoData per band (9.0, #854).** A
   dataset-wide setter cannot express a Byte mask's integer sentinel next to a
   Float32 band's NaN: the flatten operator set NaN across a
   gamma0+validity-mask output, so the mask's declared-by-convention 255
   pixels read back as valid foreground data. Writers of mixed-dtype or
   mask-carrying outputs use `GdalStreamingOutput::setBandNoDataValue` for
   every band — the two SAR terrain operators are the reference
   (band 1 NaN, mask 255). A dataset-wide `setNoDataValue` call in a file
   that writes a typed mask band is a review-rejected pattern, mechanically
   guarded by `tests/test_semantic_drift_9.cpp`. Format constraint: GeoTIFF
   serializes ONE `GDAL_NODATA` tag per dataset (GDAL warns and reuses the
   last value for every band on re-open), so the mask sentinel 255 is
   written LAST and is the persisted declaration — order matters; a NaN tag
   would make the mask's 255 read back as valid.
5. **Hydrology flood boundary includes NoData adjacency (9.0, #848).**
   `TerrainFlow::fillDepressions` seeds the priority-flood on the rectangular
   rim AND every valid cell adjacent (8-neighbourhood) to NoData —
   reprojected/clipped DEMs carry NoData borders, and rim-only seeding left
   interior depressions unfilled (the flood returned success having filled
   nothing). NoData cells remain barriers (never filled, never routed
   across); water overflowing a NoData edge leaves the known surface at the
   seed's own elevation. Contract in `terrain_flow.h`; regression in
   `tests/test_scientific_defects_9.cpp`.

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
| **Sample** (÷N−1) | unbiased estimate for inference/uncertainty | RX anomaly covariance (`spectral_anomaly.cpp`), `rs:temporal_anomaly` baseline z-score (`sampleStddev`), regression RMSE with df correction (`OnlineRegression::rmse` ÷(N−2)), feature normalization scaler |

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
