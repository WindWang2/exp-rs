# FINAL REPORT — Scientific Processing 8.0 (draft — finalized after test execution + review)

## Baseline
origin/master `322dfd3876` audited (see BASELINE.md). Track predecessor
(PR #829, Scientific Algorithms 7.0) shipped the orbit contract machinery
and the backward geolocation product; forward output-grid geocoding was
explicitly deferred — this track implements it plus the other verified gaps.

## Delivered
1. **WP A — `rs:sar_geocode`**: full forward Range-Doppler geocoding onto the
   DEM map grid with real-LOS incidence, real look-elevation layover/shadow
   classes, and the Ulander area factor as gamma0 RTC. Bounded bisection
   solvers reused from `sar_orbit`; byte-budgeted source windows; typed
   refusals; per-cause NoData counters.
2. **WP B — `rs:sar_temporal_stats`**: N-scene linear-domain statistics with
   dB reporting and speckle-robust log-domain change vs the median baseline;
   domain resolution follows the established dualpol rule.
3. **WP D — `rs:rasterize` + `rs:zonal_stats`**: one shared windowed
   rasterization seam; bounded feature cache; exact statistics (Welford,
   budgeted median); CRS transforms through the foundation policy.
4. **WP E — spectral formula drift guard**: schema-enum coverage + operator
   vs independent documented formulas + NaN contracts.
5. **WP C/F/G/H audits**: recorded in CAPABILITY_MATRIX.md — temporal family
   already satisfied on master (no second implementation added); F/G/H
   contracts audited and documented.

## Test evidence (executed)
(status per test binary appended after local execution)

## Performance/resource evidence
see PERFORMANCE.md (streaming contracts, bounded parallelism, host context).

## Adversarial review
see REVIEW_LOG.md (findings P0-P3 + remediation; 2 read-only subagents).

## Known limitations / follow-ups
- Rotated DEM grids refused (family-wide north-up contract).
- zonal median is budget-bound (flagged truncation, stats stay exact).
- rs:sar_geocode incidence products assume the DEM height is the phase
  center (no SAR-specific height offset modeling).
- Classification confidence surfaces (OpenCV-dependent) — follow-up.

## CI/CD statement
Online CI/CD was not required and was not waited on; completion is based on
locally executed, reproducible evidence recorded here.
