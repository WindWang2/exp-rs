# Temporal Remote Sensing 3.0 — Platform 10.0 Architecture

Addendum to `docs/temporal/ARCHITECTURE.md` (1.0) and `ARCHITECTURE_V2.md`
(2.0 workspace). Temporal 10.0 closes the four platform gaps that the D3 lab
content track registered in `ISSUES.md` and turns the operator family into a
modeling platform:

| Gap | Evidence at baseline | Close |
|---|---|---|
| T-1 monitor refuses `scenes` | `rs_temporal_monitor_operator.cpp` required `{collection,...}` | schema declares `scenes`; `parseCollection` already accepted both |
| T-2 no regular calendar | gap-fill output bands = input scene dates | `rs:temporal_regularize` + `temporal_calendar` kernel |
| T-3 no joint seasonal+trend change | harmonic fit global; breakpoints linear-only | `rs:temporal_harmonic_breaks` + `temporal_change` kernel |
| C-2 single-ROI extraction | `point`/`polygon` mutually exclusive | `rs:temporal_extract_regions` + `temporal_region_table` kernel |
| G terminal-JSON features | no ML artifact | `rs:temporal_region_features` typed table + schema sidecar |

## 1. Design (why these seams)

- **Kernels stay pure and thin operators stay thin.** All new math lives in
  `sicnu::temporal` (known-answer testable without GDAL); the four new
  operators only parse parameters, run `prepareTemporalRun`, gather per-tile
  series, call kernels, and write outputs — the same shape as the gap-fill
  operator. No kernel duplicates another: the joint change model reuses
  `piecewiseLinearTrend` for residual break search, the calendar Whittaker
  reuses `whittakerSmooth`, and the dense solver moved to a shared detail
  header (`temporal_linalg_detail.h`) instead of growing a copy.
- **Time semantics unchanged**: real UTC instants, day offsets from the
  collection epoch, date-vs-datetime precision preserved. The regular
  calendar is *derived* from those instants, never from array indices.
- **Provenance of filled values is a first-class output.** Every
  regularized point carries `validObservations` + `filled`; the operator
  writes `valid_count` + `filled_count` bands so downstream consumers can
  mask synthetic data. Region tables report `emptyCells` for omitted
  region × date cells instead of padding NaN rows.
- **Refusal over guessing**: no method extrapolates past the observed span;
  polygons without a single contained pixel center are refused; median
  beyond its scratch budget degrades to NaN with `medianEnabled=false` —
  all typed, surfaced in results, never silent.

## 2. The four operators

```text
rs:temporal_regularize      scenes/collection + cadence + method →
                            reg_<date> bands + valid_count + filled_count
rs:temporal_harmonic_breaks scenes/collection + harmonics/maxBreaks/... →
                            breaks_count, break_day_k, break_mag_k,
                            slope_first/last, rmse, r2 (+ onset/recovery)
rs:temporal_extract_regions regions/regions_file → region × date CSV table
rs:temporal_region_features regions/regions_file → per-region feature CSV
                            + exp_rs_temporal_region_features/1 sidecar
```

Extensions: `rs:temporal_monitor` accepts `scenes` (T-1);
`rs:temporal_smooth` gains `whittaker_robust`; `rs:temporal_phenology` gains
`cycles=2` double cropping (`c2_*` bands + `cycle_count`).

## 3. Multi-region execution shape

The date loop is outermost; each scene's raster is touched only inside
region windows, and each scene file is opened once per execution
(`TemporalTileReader` precedent). Peak working set is O(R) accumulators +
O(total region-window pixels) geometry (the parsed inside-pixel offsets)
plus one bounded median scratch (CSR layout over regions) — independent of
the image outside the region windows and of the date count; a single
region window is itself capped by the 4 M-pixel per-region guard. Cancellation checkpoints run per date and every 1024
regions. The 100k-region × 100-date target runs in O(scenes × window
pixels) I/O + O(scenes × regions) row emission — no O(T²) anywhere; the
Sen-trend features are O(dates²) per region and documented as such.

## 4. ML feature artifact

`exp_rs_temporal_region_features/1`: `region_id` column + fixed feature
order (quality → distribution → trend → anomaly → change → per-cycle
phenology → years_covered), missing token `nan`, sidecar JSON pins
`featureNames` for reproducible training. Join by `region_id` with label
tables (dataset foundry samples). A later track may register the artifact as
a dataset sample source; this track deliberately keeps it a plain table.

## 5. Deliberately not built

Full BFAST/CCDC ports (no iterative alternation, no L1/model selection);
GUI dialog changes (schema-form surfaces the operators automatically); Data
Fabric cube storage; SAR kernel changes; per-segment model selection.
