# ADR 0161: Spatiotemporal Data-Cube Tiling Protocol & Timeline Event-Stream Contract (Temporal Phenology Timeline Studio)

## Status

Accepted (track `zcode/temporal-phenology-timeline`, baseline `007e70cff6`).

## Context

D16 adds a long-time-series workbench to the platform: regularized (16-day) NDVI time cubes,
phenology retrieval, harmonic breakpoint detection, Theil-Sen/Mann-Kendall trends, STARFM-style
fusion, an interactive timeline/profile UI, and agent-facing temporal tools. The existing temporal
lineage covers per-pixel kernels (ADR 0148: `temporal_fit` free functions) and a window-read fabric
cube (`geospatial/fabric/virtual_cube.h`, D-1005..D-1007), but nothing owns a *calendar-regularized,
out-of-core, per-scene* cube with compositing, nor an interactive timeline event stream.

Hardware reality at track start: 27 GB free disk, shared 16-core host with parallel agent epics
actively compiling `qgis_core`; the full heavy build tree is ~14 GB.

## Decision

1. **`TemporalCube` as the temporal regularization layer** (`src/core/temporal_cube.h`, namespace
   `sicnu::temporal`). It composes per-scene acquisitions onto a regular calendar
   `t_k = t0 + k·Δt` (Δt = 16 d, epoch from config). For each grid node the cube gathers
   observations inside `±maxWindowDays` (32 d), scores them
   `Q_i = (1−cloud_i)·exp(−(t_i−t_k)² / 2σ_t²)`, σ_t = W/2, and composites by BestPixel (argmax Q)
   or WeightedMean. A node is **NaN** when the window holds no valid observation or the bracketing
   acquisitions are more than 45 days apart — the no-fake-interpolation rule; smoothing/fitting
   downstream treats NaN as absent, never as zero.

2. **Out-of-core tiling + LRU**: pixels are read only as 256×256 spatial tiles; at most 10 composed
   tiles (t-major float planes) are resident (≈12 MB each, ≈120 MB ceiling), so memory is O(tile·T),
   never O(scene·T). Tile reads go through the Qt-free `geospatial/raster/raster_reader.h`
   `RasterReader` window contract (ADR 0130/0134 lineage); scene instants come from ISO dates in
   file names (`YYYY-MM-DD`, `YYYYMMDD`, `YYYYDDD`) with typed refusal when unparseable; a second
   raster band, when present, is the per-pixel cloud fraction.

3. **D16 scientific core as its own static library `sicnu_temporal_timeline`**, declared in
   `src/processing/CMakeLists.txt`, holding every D16 seam implementation
   (`core/temporal_cube.cpp`, `processing/algorithms/{temporal_smoothing,phenology_metrics,
   breakpoint_detection,trend_analysis,spatiotemporal_filter}.cpp`, `agent/spatial_tools/
   temporal_spatial_tools.cpp`, `agent/tools/temporal_tool.cpp`, the two timeline widgets).
   It links `Qt6::Core/Gui/Widgets`, `GDAL::GDAL`, jsoncpp, and `Sicnu::Geospatial` — deliberately
   **not** `qgis_core`/`sicnu_processing`/`sicnu_agent`, which would force a multi-hour core build
   into every D16 test (DECISIONS D-160-1). Public seams carry the spec-mandated signatures;
   production wiring into operators/app is the declared follow-up integration.

4. **Overload adjacency rule**: the new `temporal_smoothing.h` `whittakerSmooth(y,w,λ,d=2)` and the
   existing `temporal_fit.h` `whittakerSmooth(y,w,λ)` must not appear in one translation unit;
   D16 code and tests include only the new header (DECISIONS D-160-4).

5. **Timeline event-stream contract** (GUI): `TimelineScrubberWidget` emits `dateChanged(index,
   isoDate)` and `playbackFinished`; `TemporalProfileWidget` emits `sampleHovered(tDays, value)`
   and accepts `setScrubberHoverDate`; the `TemporalTimelineWidget` composite forwards slice changes
   to both. Static chrome (grid, phenology band) is cached in a `QPixmap`; only the dynamic cursor
   layer repaints during scrub — the 60 fps redline (single frame < 16 ms) is a tested contract,
   not an aspiration. Snap-to-acquisition engages within 5 px.

6. **Significance testing stays closed-form**: Whittaker via banded Cholesky O(n) (Eilers);
   breakpoint significance via exact F-test p-values (regularized incomplete beta) with a strict
   BIC decrease gate; trend via Gilbert tie-corrected Mann-Kendall with `p = erfc(|Z|/√2)`.
   No asymptotic MOSUM boundary tables (DECISIONS D-160-6); no spec-arithmetic copied into tests
   (the D16 spec's Gilbert Var(S)=124.6667 is an erratum; the formula-exact 124.0 is asserted).

## Consequences

- D16 delivers an isolated, fully tested scientific core + widgets + agent tools without touching
  the heavy chain; integration into `rs:temporal_*` operators and the workbench shell follows as a
  separate, low-risk change once consumers want it.
- Two whittaker entry points coexist (free-function pair in `temporal_fit.h`, D16 seam in
  `temporal_smoothing.h`); the header comment documents the include rule.
- `data/labs/lab8_temporal_analysis.lab.json` uses a distinct lab id (`temporal_phenology_timeline`)
  alongside the existing `lab8_temporal_analysis.labspec.json` (id `temporal_analysis`).
