# Temporal Remote Sensing 1.0 — Architecture Plan

Multi-temporal / spatiotemporal analytics layered on the existing exp-rs stack.
Branch: `feat/temporal-rs-analysis` (based on `origin/master` @ `85b27f6f0c`).

## 1. Existing infrastructure this builds on (verified by code reading)

| Capability | Existing facility | Disposition |
|---|---|---|
| Windowed GDAL IO | `GdalDatasetWrapper` (`src/processing/gdal/gdal_dataset_wrapper.h`): `readBandWindow`, `readWindowBip`, `writeBandWindow`, `create`, NoData/metadata queries | REUSE |
| Tile streaming pattern | `rs_mosaic_operator.cpp` / `rs_change_streaming.cpp`: per-tile loop, per-execution dataset-handle reuse, `OutputFileCleaner` RAII, `context.throwIfCancelled()` per tile | REUSE pattern |
| Grid compatibility | `sicnu::data::compareGrids` + `processing::gridFromDataset` (`src/data/raster_grid_compat.h`, `gdal_grid_compat.h`) — CRS / pixel size / sub-pixel origin / extent verdicts | REUSE |
| Radiometric state | `SatelliteProducts::readRadiometricState` (`SICNU_RADIOMETRIC_STATE` metadata; radiance / toa_reflectance / surface_reflectance / brightness_temperature / digital_number) | REUSE |
| Band roles | `sicnu::data::BandRole` + `SICNU_BAND_ROLE` band metadata (written by product import `stackToGeoTiff`) | REUSE + add shared resolver |
| Index kernels | `SpectralIndices::ndvi/evi/savi/ndwi/ndbi/mndwi` + `MathUtils::normalizedDifference` (`src/processing/algorithms/spectral_indices.*`, `math_utils.*`) | REUSE — temporal index series calls the *same* kernels |
| QA / cloud masks | `QaMask::landsatQaMask / sclMask / genericBitmaskMask` (`src/processing/algorithms/qa_mask.*`) | REUSE |
| NoData→NaN normalization | `readTileBip` idiom in `rs_change_streaming.cpp` (declared finite NoData and non-finite → quiet NaN, in float space) | REUSE via shared temporal reader |
| Operator framework | `RSOperator`, `RSOperatorContext`, `rs_schema.h` builders, `rs_json_params.h`, `resource_estimation.h` | REUSE |
| Registration | `REGISTER_RS_OPERATOR` in `rs_operators_init.cpp` → auto-mirrored into `AtomicAlgorithmRegistry` (ADR 0120), auto-exposed to MCP/CLI/copilot | REUSE (zero new metadata surfaces) |
| Execution / cancel | TaskCenter → JobEngine → `RSOperatorContext::throwIfCancelled()`; `OutputCommitter` registers outputs + `DerivationRecord` | REUSE |
| Agent tools | `SpatialToolRegistry` (ADR 0122) — lightweight, schema-carrying, direct-execution tools | REUSE for `temporal:*` collection tools |
| UI | `RasterProcessingDialogBase` + `SicnuUi::` helpers + `SicnuDialogHelp` + `RsEmptyStateWidget`; menu/ribbon registration in `main_window_menus.cpp` / `ribbon_controller.cpp` | REUSE |
| Tests | hand-rolled Catch2 targets (`test_qa_mask` model), `sicnu_link_jsoncpp`, `sicnu_discover_tests` | REUSE |

**Forbidden by the goal and confirmed absent**: no second DataManager, workflow
engine, raster IO stack, agent loop, or algorithm registry is created. Temporal
is a capability layer: new kernels under `sicnu_processing`, new operators under
`sicnu_operators`, new tools under `sicnu_agent`, one dialog under `src/app`.

## 2. Placement

The goal suggests `src/analysis/temporal/`; in this codebase the equivalent,
architecture-consistent location for RS science kernels is
`src/processing/algorithms/` (compiled into `sicnu_processing`, which already
owns the GDAL wrapper, grid-compat service and satellite-product metadata) —
`src/analysis/` itself holds QGIS-vendored code and phase-specific static libs
that must not depend on `sicnu_processing`. New code therefore lands in:

```
src/processing/algorithms/temporal/     (core library, target sicnu_processing)
    temporal_time.h/.cpp            AcquisitionTime: ISO-8601 parse, date vs
                                    datetime precision, day offsets, sorting key
    temporal_collection.h/.cpp      TemporalSceneRef + TemporalCollection:
                                    metadata + references (never raster copies),
                                    JSON descriptor save/load, deterministic
                                    ordering, duplicate policy
    temporal_validity.h/.cpp        shared validity contract (declared finite
                                    NoData / NaN / QA-cloud mask → valid flag)
    temporal_band_roles.h/.cpp      shared role→band resolver (SICNU_BAND_ROLE
                                    metadata + positional fallback + warning),
                                    shared by all temporal operators
    temporal_preflight.h/.cpp       PreflightReport: time / spatial / spectral /
                                    radiometric / validity checks (§9) built on
                                    compareGrids + readRadiometricState
    temporal_stream.h/.cpp          TemporalTileReader: bounded-memory
                                    tile × per-date streaming reader; per-
                                    execution dataset handles; NoData→NaN
                                    normalization; QA mask application; buffer
                                    accounting (peak slots) for memory tests
    temporal_stats.h                Welford accumulators (mean/variance) and
                                    online-covariance linear-regression
                                    accumulators (West's algorithm) — header-only

src/operators/rs/                       (thin operators, target sicnu_operators)
    rs_temporal_collection_input.h   shared param parsing: "collection" (JSON
                                    descriptor path) or inline "scenes" array →
                                    TemporalCollection (one canonical entry point)
    rs_temporal_summary_operator.*     rs:temporal_summary
    rs_temporal_composite_operator.*   rs:temporal_composite  (best-pixel /
                                       mean / median × period grouping)
    rs_temporal_index_series_operator.* rs:temporal_index_series (reuses
                                       SpectralIndices kernels per date)
    rs_temporal_trend_operator.*       rs:temporal_trend (slope/intercept/R²/
                                       n/RMSE, real time intervals)
    rs_temporal_anomaly_operator.*     rs:temporal_anomaly (baseline mean
                                       difference / z-score)
    rs_temporal_extract_series_operator.* rs:temporal_extract_series (point /
                                       polygon ROI series → CSV + JSON)

src/agent/spatial_tools/temporal_collection_tools.*   temporal:create_collection /
                                        describe_collection / list_scenes /
                                        preflight_collection (SpatialTool model)

src/app/dialogs/temporal_analysis_dialog.*  Temporal Analysis entry (scene list,
                                        time column, role/grid/radiometric status,
                                        algorithm + parameters, run/cancel)
```

## 3. Data model

`TemporalSceneRef` = **metadata + reference** (path now; optional DataManager
`assetId`+`revision` fields for future asset-bound use). No raster copies, no
RAM cube, no implicit cache. The collection descriptor is a small JSON document
(versioned schema) that can be saved/reloaded; ordering + time metadata are
preserved on reload (§30). Persistence uses a workspace sidecar file — the same
philosophy as `VirtualRasterRecipe` (identity = JSON of references, assets stay
owners of the pixels). We do not touch `DataManager` internals or the project
serializer in v1.

Acquisition time is mandatory scientific metadata (§7): parsed from product
metadata (`SICNU_ACQUISITION_DATE`), explicit user input, or filename
(Sentinel-2 `_YYYYMMDDTHHMMSS`, `YYYYMMDD`, Landsat, MODIS `AYYYYDDD`).
**Date vs datetime precision is preserved** — a date-only value is never
silently interpreted as an overpass instant. Sorting is by instant with a
deterministic (time, original index) tiebreak; duplicate timestamps follow an
explicit policy (`keep_all` default / `reject`), never silent randomness (§8).

## 4. Scientific contracts (enforced by preflight, §9–§11, §27)

1. **Time**: count, missing timestamps, duplicates, range, ordering.
2. **Spatial**: every scene must share the reference scene's grid —
   `compareGrids` + exact width/height (same-grid contract, like change
   detection). Mismatch ⇒ blocking `temporal.grid_mismatch`. **No hidden
   resampling**: aligning is an explicit upstream `gdal:reproject` /
   `gdal:clip` step.
3. **Spectral**: every requested band role must resolve in every scene
   (role metadata or explicit per-scene override), else blocking.
4. **Radiometric**: `SICNU_RADIOMETRIC_STATE` equality across scenes
   (non-empty differing states ⇒ reject, mirroring change detection); GDAL
   per-band scale/offset must be *uniformly declared and identical* across
   scenes — all-or-none, else reject. When declared, scale/offset is applied
   as an explicit documented normalization on read (§45: float 0.1/0.2/0.3 and
   scaled-int 1000/2000/3000 @ scale=1e-4 produce identical results).
   No `max>1 ⇒ /10000` guessing — ever.
5. **Validity**: one shared contract for every temporal operator — a sample is
   valid iff finite, not equal to the band's declared NoData, and not masked by
   the scene's QA/cloud mask (Landsat QA bits / Sentinel-2 SCL classes via the
   existing `QaMask` kernels) when masking is enabled.

Missing data stays missing: no interpolation (§28); outputs use NaN/NoData and
per-pixel observation counts.

## 5. Streaming & memory design (§12–§13, §48)

All temporal algorithms iterate **spatial tiles** (default 256×256) and, within
a tile, **dates sequentially** — reading one date's tile, folding it into
constant-size per-pixel accumulators, then discarding the buffer. Peak working
set is therefore `O(tilePixels × activeVariables)` and **independent of date
count**: summary keeps 5 floats/pixel (n, mean, M2, min, max), trend 6
(n, Σt, Σy, and centered M_tt/M_ty/M_yy via West's online update), composite
keeps value+count+best-score. Exact median/percentile materializes per-pixel
values for one *reduced* tile at a time (tile auto-shrink so
`T × tilePixels × 4 B ≤ budget`, default 256 MiB) — exact, documented, never
`T × H × W` (§14). Dataset handles are opened once per execution and reused
across tiles (mosaic pattern); GDAL datasets are used from the operator thread
only (no cross-thread sharing). `TemporalTileReader` carries instrumented
buffer accounting (`peakFloatSlots`) surfaced in operator results so tests can
assert bounded memory at 20/50/100 dates without OOM roulette.

## 6. Algorithm semantics

* **temporal_summary** — count / valid_count / mean / min / max / stddev
  (Welford), median (exact, tile-shrunk). Statistics hand-checkable.
* **temporal_composite** — best-pixel selection by explicit deterministic
  score: valid pixels only; maximize quality value (quality band, or 1.0);
  tie ⇒ smallest |t − t_target| (target date, default period midpoint);
  tie ⇒ lowest scene index. Outputs value + **observation count** + selected
  quality (§16–§17). Mean/median composites reuse the summary kernels.
  Optional period grouping (all / month / quarter / season / year /
  custom N days) with time metadata preserved per output (§18).
* **temporal_index_series** — per date, band roles → shared resolver →
  *the same* `SpectralIndices` / `MathUtils` kernels the single-scene operator
  uses; output is one stacked GeoTIFF, one band per date, per-band
  `SICNU_ACQUISITION_DATE` + role metadata; kernel-equality is asserted by
  test against `rs:spectral_index` output (§19–§20).
* **temporal_trend** — per-pixel ordinary least squares with **real time
  intervals** (`t = days since collection reference epoch`, §22), numerically
  stable online covariance (§23), outputs slope / intercept / R² / n / RMSE
  (§21). n<2 ⇒ NoData.
* **temporal_anomaly** — difference-from-baseline-mean and z-score (sample
  stddev); baseline selected by an explicit date range (default: full
  collection minus the target scene and its duplicate-instant twins);
  stddev==0 ⇒ z-score NoData (difference stays defined); insufficient
  baseline (< `min_observations`, default 2) ⇒ NoData + warning (§24).
  Same-calendar-month seasonal baselines are a documented follow-up.
* **temporal_extract_series** — point (map → pixel per scene) and polygon ROI
  (bbox-windowed, polygon rasterized inside the bbox only — §26) series with
  mean/median/min/max/stddev/valid_count; output CSV + JSON series.

Determinism (§50): stable sort, explicit tie-breaks, no unordered iteration in
output paths. Cancellation (§49): checked per tile and per date; RAII cleaners
delete partial outputs; existing valid outputs are never touched.

## 7. Provenance & registration

Operators return the standard `output` JSON; when run through TaskCenter the
existing `OutputCommitter` registers the asset and attaches a
`DerivationRecord` (source algorithmId + parameters). Temporal outputs
additionally embed `SICNU_TEMPORAL_*` dataset metadata (scene count, time
range, algorithm, quality policy) following the `SICNU_CHANGE_*` precedent
(§29). No second provenance framework.

## 8. Agent / Pi integration (§33–§35)

Processing algorithms are exposed through the existing
`search_tools → get_tool_schema → preflight → execute` chain automatically.
Collection management gets four lightweight `SpatialTool`s
(`temporal:create_collection`, `describe_collection`, `list_scenes`,
`preflight_collection`) — compact by default (§35), paged on request. MCP
allow-list gains the `temporal:` prefix; Pi's default category string gains
`temporal`. No new agent loop.

## 9. What this goal deliberately does NOT build

STAC client, COG/HTTP cache, physical cube database, phenology models
(double-logistic / Whittaker / BFAST / LandTrendr), interpolation framework,
chart engine (v1 ships table/CSV/JSON), a second DataManager or provenance
framework. Interfaces keep an ingestion-adapter seam for future STAC.

## 10. Conflict control vs parallel PRs (#708/#709/#710)

New code lives in new files (`src/processing/algorithms/temporal/`,
`rs_temporal_*_operator.*`, `temporal_collection_tools.*`,
`temporal_analysis_dialog.*`). Integration touches are minimal and additive:
registration lines, one CMake source list per target, two lines in
`mcp_server.cpp`, one string in `pi/exp-rs-spatial.ts`, menu/ribbon entries,
test registrations.
