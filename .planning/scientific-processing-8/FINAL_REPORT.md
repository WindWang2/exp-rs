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

All binaries built and run locally (clang 22.1.8, Debug, ccache;
`cmake --build build --target <t> -j 2` from build/):

| Suite | Result |
|---|---|
| test_sar_geocoding | All tests passed (958 assertions in 7 test cases) |
| test_raster_vector | All tests passed (119 assertions in 7 test cases) |
| test_sar_temporal_stats | All tests passed (238 assertions in 5 test cases) |
| test_spectral_formula_drift | All tests passed (145 assertions in 4 test cases) |
| test_algorithm_meta_drift | All tests passed (2270 assertions; extended sidecars) |
| test_sar_operators | All tests passed (530 assertions, 16 cases) |
| test_sar_kernels | All tests passed (89 assertions, 12 cases) |
| test_catalog_size | All tests passed (budget raised 160→176 KiB with justification) |
| test_algorithm_organization | All tests passed (593 assertions, 9 cases) |
| test_algorithm_schema | All tests passed (31 assertions) |
| test_harness_catalog | All tests passed (93 assertions, 5 cases) |
| test_capability_drift | 13/15 cases — 2 failures PRE-EXISTING on master (zero diff in src/agent, the test file, and agent data; see REVIEW_LOG) |

## Performance/resource evidence
see PERFORMANCE.md (streaming contracts, bounded parallelism, host context).

## Adversarial review
see REVIEW_LOG.md (findings P0-P3 + remediation; 2 read-only subagents).

## Known limitations / follow-ups
- Rotated DEM grids refused (family-wide north-up contract).
- Zonal median is budget-bound (flagged truncation, stats stay exact).
- rs:sar_geocode incidence products assume the DEM height is the phase
  center (no SAR-specific height offset modeling); the over-budget source-
  window fallback (per-pixel 2×2 reads) is bounded-but-untested (would need
  a fixture with a diagonal pass over a large tile).
- Classification confidence surfaces (OpenCV-dependent) — follow-up.
- Pre-existing master failures in test_capability_drift (2 cases, harness
  plan reader / runtime meter) are outside this track's ownership; see
  REVIEW_LOG for the zero-diff evidence.

## CI/CD statement
Online CI/CD was not required and was not waited on; completion is based on
locally executed, reproducible evidence recorded here and in TEST_MATRIX.md.
