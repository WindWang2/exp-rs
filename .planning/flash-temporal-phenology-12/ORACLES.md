# ORACLES — flash-temporal-phenology-12

Objective completion conditions. Each maps to a reproducible check.
Build dir: `build-dev/`; tests run with `QT_QPA_PLATFORM=offscreen`, `-j1`.

## O1 — Time-axis contract (WP1)

Irregular / duplicate / missing / cross-year / leap-year fixtures produce stable
results; malformed inputs fail with *typed* errors.

- [x] Duplicate acquisitions: `TemporalCollection` ordering + keep_all/reject
  policy — `tests/test_temporal_core.cpp` ("Temporal collection ordering and
  duplicates"). Kernel-level duplicate-axis refusal:
  `test_temporal_irregular` ("whittakerSmoothTime refuses duplicate/degenerate
  axes", movingAverageDays/savitzkyGolayDays dup cases).
- [x] Missing timestamp → typed error: `rs:temporal_smooth` day-axis methods
  fail `InvalidInputData` naming the untimed scene (fail-fast, pre-output).
- [x] Irregular cadence processed on real day offsets: all `*_days` kernels +
  `tDays` plumbing (`reader.sceneDayOffset`).
- [x] Leap-year fixture: "time-aware kernels honor the leap day on a real
  calendar axis" — 2024-02-28/29/03-01 spacing + linear-signal reproduction +
  gap-fill midpoint over the leap day.
- [x] Cross-year wrapped season: "phenologyThreshold keeps metrics consistent
  across a year boundary".
- [x] Ambiguous/inline modality vs metadata: `test_spatiotemporal_contracts.cpp`
  ("Explicit inline modality wins over product metadata"); no filename-guessing
  introduced.

## O2 — Synthetic seasonal recovery (WP3+WP4)

- [x] Harmonic fit on irregular timestamps (existing `test_temporal_fit`
  suite; kernels already time-aware, robust IRLS on tDays).
- [x] Phenology SOS/EOS/peak within tolerance vs analytic piecewise-linear
  reference: "phenologyThreshold reports limb rates and midpoints on an
  irregular axis" (hand-derived crossing times).
- [x] Typed refusal: "leaves limb metrics undefined when the window opens
  mid-ramp" / "on a flat season" — NaN/-1, never fabricated.

## O3 — QA-mask correctness (WP2)

- [x] Per-sample provenance: `SampleProvenance{Unavailable,Observed,
  Interpolated}` via `gapFillProvenance()` + `rs:temporal_gap_fill`
  `provenance_output` UInt8 sidecar (prov_<date> bands, codes documented in
  dataset metadata).
- [x] Observed-vs-interpolated distinction: "gapFillProvenance labels
  observed, interpolated and unavailable".
- [x] NoData never yields finite output: unfillable gaps stay NaN
  (gapFillSeries no-extrapolation contract, covered).

## O4 — Streaming memory bound (WP7)

- [x] `TemporalTileReader::estimateWorkingSetBytes` asserted tile-quadratic /
  raster-independent: "working-set estimate is tile-bounded, not
  raster-bounded". Operators keep the 2 GiB series-gather OOM guard.

## O5 — Suite stability

- [ ] `ctest -R "test_temporal_irregular|test_temporal_fit|test_temporal_operators_10|test_temporal_uncertainty|test_temporal_core|test_temporal_calendar"` twice, green.

## Gatekeeping

- [ ] `git diff --check` clean; no unrelated churn.
- [ ] `test_contract_census_11` snapshot regenerated via
  `contract_inventory --census-out` (new operator must appear).
- [ ] Independent review P0/P1 = 0 before PR.
