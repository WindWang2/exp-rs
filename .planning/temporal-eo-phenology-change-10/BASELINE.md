# BASELINE — temporal-eo-phenology-change-10

- **Baseline SHA**: `7d78059d1a6d316d606656759a506d17bc5e3b55` (`origin/master`, verified `git rev-parse origin/master` == `git rev-parse HEAD` on 2026-09-13).
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-temporal-eo-phenology-change-10`
- **Branch**: `zcode/temporal-eo-phenology-change-10` (created from `origin/master`, no prior residue: `git worktree list` + `git branch -a | grep temporal` showed none).
- **Concurrent 10.0 tracks observed** (same baseline): `zcode/cn-eo-products-sensor-physics-10`, `zcode/scientific-contract-verification-10`; additional worktrees exist for `advanced-sar-polsar-insar-10`, `cloud-data-fabric-datacube-10`, `hyperspectral-spectral-intelligence-10` (uncommitted work may exist on those branches — shared-file changes must be append-only/narrow).

## Host resources

- 16 cores / 62 GB RAM; build discipline: `-j2`, drop to `-j1` under pressure; `-j$(nproc)` forbidden.
- Disk: `/` at 94% used, 56 GB free at track start. A full `build-dev` tree is ~13 GB (measured in `main/build-dev`). Targeted builds only; monitor `df -h` per phase.

## Verified current capability matrix (baseline, `file:line` evidence)

| Operator | Input | Time source | Status at baseline |
|---|---|---|---|
| `rs:temporal_summary` | scenes+collection | instants | registered `rs_operators_init.cpp:174` |
| `rs:temporal_composite` | scenes+collection | instants | `:175` |
| `rs:temporal_index_series` | scenes+collection | instants | `:176` |
| `rs:temporal_trend` | scenes+collection | real day offsets | `:177` |
| `rs:temporal_smooth` | scenes+collection | **index axis** (uniform-spacing assumption, `docs/processing/temporal.md:14`) | `:178` |
| `rs:temporal_gap_fill` | scenes+collection | real day offsets; **no regular-calendar output** (T-2 verified: output bands = input scene dates, `rs_temporal_gap_fill_operator.cpp:229-232`) | `:179` |
| `rs:temporal_harmonic_fit` | scenes+collection | real day offsets, single global fit (T-3 half) | `:180` |
| `rs:temporal_phenology` | scenes+collection | doy/real days, **single season window** (`temporal_fit.h:60-80` SeasonalMetrics singular) | `:181` |
| `rs:temporal_breakpoints` | scenes+collection | real day offsets; piecewise *linear* only (no per-segment seasonality) | `:182` |
| `rs:temporal_sen_trend` | scenes+collection | real day offsets | `:183` |
| `rs:temporal_decompose` | scenes+collection | **index axis** (`docs/processing/temporal.md:37`) | `:184` |
| `rs:temporal_anomaly` | scenes+collection | instants | `:185` |
| `rs:temporal_extract_series` | scenes+collection | single point OR single polygon only (**C-2 verified**: `rs_temporal_extract_series_operator.cpp:118` `point` mutually exclusive `polygon`) | `:186` |
| `rs:temporal_monitor` | **collection only** (**T-1 verified**: schema required `{collection, output, method}`, `rs_temporal_monitor_operator.cpp:77`) | instants | `:160` |
| `rs:sar_temporal_stats` | scenes | — | `:158` (SAR kernel — NOT this track's ownership) |

## Baseline kernel inventory (`src/processing/algorithms/temporal/`)

- `temporal_time.h` AcquisitionTime (date/datetime precision, ISO-8601, filename parsers).
- `temporal_collection.h` TemporalCollection/TemporalSceneRef (modality/sensor/polarizations/quality descriptors round-trip through descriptor v1).
- `spatiotemporal_contracts.h` ObservationContract typed view (derived, never persisted).
- `temporal_preflight.h/.cpp` time/spatial/spectral/radiometric/validity gate.
- `temporal_stream.h/.cpp` TemporalTileReader: tile×date streaming, NoData/QA→NaN, buffer instrumentation.
- `temporal_stats.h` Welford + West online regression.
- `temporal_fit.h/.cpp` SG, Whittaker, harmonicFit(+robust IRLS), phenologyThreshold (single season), piecewiseLinearTrend (BSFAST-lite), mannKendallSenSlope, seasonalDecompose.
- `temporal_gapfill.h/.cpp` Linear/Nearest gap fill, max-gap, no extrapolation.
- `temporal_monitoring.h/.cpp` CUSUM, EWMA, seasonal MK.
- `temporal_workspace.h/.cpp` ADR 0125 record binding/fingerprint; `temporal_stac_adapter.h/.cpp` STAC ingestion.

## Issue status at baseline (all re-verified in code, none fixed yet)

- **T-1** `rs:temporal_monitor` collection-only — CONFIRMED (`rs_temporal_monitor_operator.cpp:77`).
- **T-2** `rs:temporal_gap_fill` no regular calendar — CONFIRMED (output band list = input dates).
- **T-3** no joint trend+seasonal segmentation — CONFIRMED (harmonic fit is global; breakpoints are linear-only).
- **C-2** `rs:temporal_extract_series` single ROI — CONFIRMED.

## Dedupe exclusions

- Temporal 1.0 (PR #712) and 2.0/3.0 (PRs #732/#733/#738/#742) already merged — streaming operators, workspace records, STAC, multimodal contracts are NOT to be re-implemented.
- Execution cache/fingerprint (#720/#732), fail-closed cache verdicts — done.
- `rs:temporal_sen_trend` (ADR 0130 Algorithms) — done.
- Data Fabric / EO cubes (geospatial-data-fabric-8, `bae9fe5593`) — separate ownership; temporal adapters must not copy it.
- SAR temporal kernels (`sar_temporal.h`) — Track: advanced-sar-polsar-insar-10 ownership; untouched here.
