# PLAN / MILESTONES — Scientific Processing 8.0

## M0 — Baseline & infrastructure (done at audit time)
- origin/master audited at `322dfd3876`; overlap map vs latest 30 PRs (BASELINE.md)
- worktree `feat/scientific-processing-8` created from origin/master
- targeted build verified (test_sar_orbit)

## M1 — WP A kernel + operator (`rs:sar_geocode`)
- `sar_geocoding.{h,cpp}` kernel: forward RD geocoding onto DEM grid,
  real-LOS incidence, layover/shadow classes, area-factor RTC, streaming
  row-block memory contract, typed refusals
- `rs_sar_geocode_operator.{h,cpp}` + registration + help/catalog sidecar
- `tests/test_sar_geocoding.cpp` (kernel known-answer/round-trip/refusal)
- operator E2E cases in the same target (synthetic GeoTIFFs + metadata keys)
- docs: sar-domain.md §4 (new authority section)

## M2 — WP D kernels + operators (`rs:rasterize`, `rs:zonal_stats`)
- `raster_vector.{h,cpp}` kernel (shared rasterization seam)
- operators + registration + sidecars
- `tests/test_raster_vector.cpp` (analytic polygons/rectangles, CRS
  transform case, NoData, allTouched, last-wins, zone spanning blocks,
  median cap, refusals)
- docs: docs/processing/raster-vector.md

## M3 — WP B kernel + operator (`rs:sar_temporal_stats`)
- `sar_temporal.{h,cpp}` + `rs_sar_temporal_stats_operator.*`
- `tests/test_sar_temporal_stats.cpp` (linear-domain known answers, dB
  conversion, robust log-change closed forms, missing/masked scenes,
  refusal matrix)
- docs: sar-domain.md §5

## M4 — WP E drift guard + audit closures
- `tests/test_spectral_formula_drift.cpp`
- temporal family audit verdict recorded; targeted strengthening only if a
  real hole is found
- F/G/H audit verdicts recorded

## M5 — Adversarial review + remediation
- two read-only review subagents (architecture/correctness; tests/
  portability/resources) over the full diff
- P0/P1 fixed; P2 fixed or justified; P3 fixed or explicitly justified
- REVIEW_LOG.md updated; affected tests re-run

## M6 — Integration & PR
- sync master if moved; full diff inspection; bounded integration sweep
- FINAL_REPORT.md; push; PR (no CI wait)
