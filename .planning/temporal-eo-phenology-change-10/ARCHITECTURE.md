# ARCHITECTURE — Temporal Platform 10.0

Temporal 1.0 shipped streaming operators over a JSON-descriptor collection;
Temporal 2.0/3.0 made the collection a first-class workspace asset (ADR 0125)
with multimodal observation contracts and STAC ingestion. Temporal 10.0 closes
the modeling gaps: regular time, joint seasonal-trend change, multi-region
extraction, and ML-consumable features. It reuses every existing authority
(`TemporalTileReader`, `temporal_fit` kernels, `prepareTemporalRun`, ADR 0125
workspace records) and introduces no second stack.

## 1. Design principles (inherited, verified)

- **Kernels pure, operators thin**: new math lives in `sicnu::temporal`
  header/impl pairs under `src/processing/algorithms/temporal/`, tested
  known-answer style without GDAL where possible; operators only parse params,
  run preflight, stream tiles, and write outputs (the `rs_temporal_gap_fill_operator.cpp`
  precedent).
- **Streaming, bounded memory**: tile × date loop via `TemporalTileReader`;
  per-pixel state O(tilePixels × model) — never O(T × H × W). Multi-region
  extraction keeps accumulators O(regions), regions streamed by date.
- **NaN = missing; real day offsets; deterministic** (`temporal_fit.h:1-12`
  contract). New kernels state their denominator rule and determinism grade.
- **Honest naming**: the joint model is a greedy per-segment harmonic+linear
  fit — described as "BSFAST/CCDC-inspired greedy segmentation", never claimed
  to be BFAST or CCDC (mirrors the existing BSFAST-lite honesty note in
  `docs/processing/temporal.md:52-56`).
- **Fail-closed refusals**: extrapolation outside the observation range is a
  refusal (NaN + `extrapolated` accounting), never a silent guess (matches
  gap-fill's no-extrapolation contract).

## 2. New kernels

### 2.1 `temporal_calendar.h/.cpp` — time normalization (T-2)

```text
CalendarSpec   { cadenceDays (int > 0) | monthly; anchorDate; }
CalendarPoint  { tDays (double, day offset from collection epoch), date (ISO) }
buildRegularCalendar(rangeStart, rangeEnd, spec) -> vector<CalendarPoint>
```

`regularizeSeries(series, tDays, calendar, options) -> RegularizedSeries`:

- `method`: `nearest` (closest observation within `maxWindowDays`, tie →
  earlier), `window_mean` (all observations inside ±window), `linear`
  (time-weighted interpolation between bracketing observations),
  `whittaker` (penalized least squares **defined on the calendar grid**:
  minimize Σ w_i (y_i − z_g(i))² + λ Σ (Δ² z)² over grid points g; banded
  solver reused; observations map to nearest grid node; interior grid nodes
  with no observation and no neighbor reach get bridged by the penalty —
  with `max_gap_nodes` guarding how many consecutive data-free nodes may be
  bridged).
- **Extrapolation refusal**: calendar points before the first / after the
  last observation stay NaN for `nearest`/`window_mean`/`linear`; `whittaker`
  also refuses (the penalty does not define values outside the data span).
- **Accounting**: per output point `validObservations` (count contributing),
  `filled` flag (synthetic value from a gap-fill rule rather than an observed
  sample) → provenance band (`*_filled` mask) in operator output.
- Deterministic: fixed order, bit-exact grade (whittaker = tolerance grade
  like `whittakerSmooth`, documented 1e-4).

The monthly cadence uses calendar months (clamped day-of-month), day cadence
uses the anchor + k·cadence — both derived from real UTC instants.

### 2.2 `temporal_change.h/.cpp` — joint harmonic+trend segmentation (T-3)

```text
SeasonalTrendBreaks
  fitSeasonalTrendBreaks(y, tDays, options)
    options: harmonics (1..3), maxBreaks, minSegment, minImprovement,
             robustIRLS (bool)
  result:
    breakIndices[]           // segment start sample indices
    breakDays[]              // calendar day offsets + ISO dates via caller
    segments[]               // per segment: slope/day, intercept, harmonic
                             // coefficients, rmse, nValid
    magnitudes[]             // |fitted(post-break) − predicted(pre-break)| at
                             // the break (change magnitude)
    rmse, r2, validCount
```

Algorithm (greedy, deterministic): OLS design matrix [1, t, sin(kωt), cos(kωt)…]
per candidate segment; repeated splitting at the break maximizing RSS
reduction while reduction/segment-RSS > `minImprovement` and both sides keep
≥ `minSegment` valid samples — the `piecewiseLinearTrend` skeleton
(`temporal_fit.cpp`) generalized from [1, t] to the harmonic design. Optional
IRLS Huber reweighting per segment (the `harmonicFit` robust path). This is
**not** BFAST (no iterative season-trend alternation, no median filtering) and
**not** CCDC (no per-segment model selection or L1) — the honest method name
is "greedy harmonic+trend segmentation (BSFAST/CCDC-inspired)".

Disturbance semantics (operator level, on smoothed/index series):
- `onset` = break date of a segment whose slope sign flips toward loss
  (caller-declared `direction`: `decrease` for NDVI-like disturbance).
- `recoveryDays` = days from onset to the first post-onset point where the
  fitted series returns above (pre-break level − tolerance) — unbounded when
  never recovered (`-1`).
- repeated events: multiple breaks naturally yield multiple onsets.

### 2.3 Multi-cycle phenology — extension of `temporal_fit.h`

`phenologyThreshold` keeps its single-season contract; a new
`phenologyCycles(y, tDays, doyOf, cyclesPerYear, season windows|auto,
crossingFraction)` computes per-cycle `SeasonalMetrics`:

- `cyclesPerYear` ∈ {1, 2} (double-cropping); cycles split the year at
  user-declared doy windows or, when `auto`, at the detected minima between
  the year's two largest peaks (documented heuristic; single-peak years
  degrade to one cycle with `splitConfidence = 0`).
- Southern/northern hemisphere: the season window is caller-declared in doy —
  the kernel stays hemisphere-neutral; the operator docs state the
  convention (the existing behavior).
- Output per cycle: sos/pos/eos/los/amplitude/base/integral + cycle index;
  `validCycleCount` per pixel drives raster band layout.

### 2.4 Robust Whittaker — `whittakerSmoothRobust(y, w, lambda, iterations)`

IRLS reweighting (asymmetric-Tsquared via 1/(1+(r/k)²) Cauchy weights, 3
iterations default) on top of the existing banded solve; exposed in
`rs:temporal_smooth` as `method=whittaker_robust`. Tolerance grade, documented.

### 2.5 `temporal_region_table.h/.cpp` — multi-ROI aggregation (C-2)

```text
RegionRef      { id (string, caller-owned), point | polygon (map coords) }
RegionAccumulator  { per-stat Welford / min / max / count }
RegionTable
  addDate(tDay, ISO date)
  accumulateRegion(regionIdx, value)          // streaming per date
  rows() -> region × date rows (CSV/JSON projection at operator level)
```

Aggregation stats per region per date: mean/median/min/max/stddev/valid_count
(median via bounded per-region value buffer; declared budget guard: regions ×
window samples ≤ 8 MB). Point regions contribute the pixel under the point
(same `mapToPixel` semantics as `rs:temporal_extract_series`); polygons
rasterize inside their bbox window (even-odd ray casting on pixel centers —
the existing `pointInPolygon` kernel, relocated to the shared kernel file so
operator and region table share one implementation). Memory: O(R) state +
one ROI window of pixels per scene. The date loop is outermost → every scene
is read once (I/O O(R·T) CPU only for in-window pixels, independent of image
size outside ROIs).

## 3. New operators (thin, `sicnu::operators::rs`)

| Operator | Params (new) | Outputs | Closes |
|---|---|---|---|
| `rs:temporal_regularize` | scenes/collection (shared `prepareTemporalRun`), band/band_role, `cadence` ("16d"/"month" + `cadence_days`), `method` (nearest/window_mean/linear/whittaker), `max_window_days`, `max_gap_nodes` (whittaker), `lambda` | GeoTIFF `T` bands + `valid_count` + `filled` provenance band; JSON `calendarStart/calendarEnd/cadenceDays/filledFraction` | T-2 |
| `rs:temporal_harmonic_breaks` | scenes/collection, band/role, `harmonics`, `max_breaks`, `min_segment_days`, `min_improvement`, `robust` | bands: slope, intercept, rmse, r2, breaks_count, first_break_day, magnitude; JSON segments (dates, slopes, magnitudes, recovery where `direction` given) | T-3 |
| `rs:temporal_extract_regions` | scenes/collection, band/role, `regions` JSON array `{id, point|[polygon]}` or `regions_file` (JSON path), stats selection, `max_regions` guard (default 100000) | CSV `region_id,date,stat…` + JSON summary + per-region validity | C-2 |
| `rs:temporal_region_features` | same region input + feature selection (phenology/trend/anomaly/change) | one row per region CSV/JSON with typed feature schema (schema `exp_rs_temporal_region_features/1`), quality counts | G |

`rs:temporal_monitor` gains `scenes` in schema + required relaxed to
`(collection|scenes)` — `prepareTemporalRun` already parses both (T-1).
`rs:temporal_smooth` gains `whittaker_robust`. `rs:temporal_phenology` gains
`cycles_per_year` + second-season window params.

## 4. ML feature artifact (G)

`rs:temporal_region_features` emits `features.csv` (header = typed feature
names, `region_id` first column) plus a JSON sidecar `{schema:
"exp_rs_temporal_region_features/1", featureNames[], regionCount, timeRange,
provenance{operator, params, collectionFingerprint}}`. Consumers: the CSV
joins with label tables in the dataset foundry by `region_id`; agents read the
sidecar schema. Documented in `docs/processing/temporal.md`. (No new
Data-Fabric/Dataset-store coupling in this track — the artifact is a plain
table; a later track may register it as a dataset sample source.)

## 5. Integration seams

- Registration: 4 `REGISTER_RS_OPERATOR` lines in `rs_operators_init.cpp`
  (append-only).
- MCP/CLI/agent surfaces are automatic through the registry mirror (ADR 0120).
- `data/processing/algorithm_meta/` capability sidecars for each new operator
  (the `rs-*.json` precedent).
- Knowledge: append entries in `data/agent/knowledge/` per the capability
  knowledge layer's file format (PR #950) — append-only.
- Tests: kernels → `test_temporal_calendar.cpp`, `test_temporal_change.cpp`
  (pure, no GDAL); operators E2E → extend `test_temporal_algorithms.cpp`
  pattern (synthetic GeoTIFF scenes); regions scale + cancellation →
  `test_temporal_regions.cpp`.
- Benchmark: `benchmark_temporal10` (Catch2 benchmark-style harness following
  `benchmark_scale8` structure) — synthetic 1000-scene sparse collection +
  100k regions; evidence numbers only, never a gate.

## 6. Explicitly NOT built (this track)

- No BFAST/CCDC full ports (documented honest variants only).
- No new raster IO / STAC / workspace machinery (reuse ADR 0125 seams).
- No SAR kernel changes (`rs:sar_temporal_stats` untouched).
- No GUI dialog work (schema-form surfaces new operators automatically through
  the descriptor contract; dedicated GUI is follow-up).
- No Data-Fabric cube storage; the TimeCube *view* remains the descriptor +
  typed `ObservationContract` (§ spatiotemporal_contracts.h) — extended only
  where a consumer needs it.
