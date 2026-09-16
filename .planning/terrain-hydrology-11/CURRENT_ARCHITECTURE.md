# CURRENT_ARCHITECTURE — terrain terrain family (master @ a5b11b7f10 + this track)

## Authority map (terrain domain)

```
Algorithm kernels (authority for math)
  src/processing/algorithms/terrain_analysis.{h,cpp}   slope/aspect/hillshade/rough/TRI/TPI/curvature/relief
  src/processing/algorithms/terrain_flow.{h,cpp}       fillDepressions/flowDirections(D8)/flowAccumulation/watershedLabels
  src/processing/algorithms/terrain_hydrology.{h,cpp}  [NEW] resolveFlats(epsilon)/flowDirectionInf(D∞)/streamNetwork(Strahler)/detectOutlets
  src/processing/algorithms/terrain_viewshed.{h,cpp}   [NEW] viewshedR3/CumulativeViewshed/HorizonGrid
  src/processing/algorithms/terrain_solar.{h,cpp}      [NEW] HorizonGrid consumers: shadowMask/shadowDuration/hillshadeSeries/lowPrecisionSunPosition
  src/processing/algorithms/terrain_landform.{h,cpp}   [NEW] multiscale TPI + Weiss classes, geomorphon classes

Operator surface (authority for params/schema/errors)
  src/operators/rs/rs_terrain_analysis_operator.*   rs:terrain_analysis  (11 products → +tpi_multiscale,+landform_class,+geomorphon)
  src/operators/rs/rs_terrain_flow_operator.*       rs:terrain_flow      (4 products → +flat_resolve,+flow_direction_inf,+stream_network,+outlets)
  src/operators/rs/rs_terrain_viewshed_operator.*   [NEW] rs:terrain_viewshed (products: viewshed, cumulative)
  src/operators/rs/rs_terrain_solar_operator.*      [NEW] rs:terrain_solar     (products: horizon, shadow_duration, hillshade_series)

Capability metadata (derived, regenerated — NOT a second truth)
  data/processing/algorithm_meta/capability/rs-terrain-*.json  (gen-meta from live descriptors)

UI / agent (consumers of the operator/registry seam)
  src/app/dialogs/terrain_dialog.*          params JSON → registry.create("rs:terrain_analysis"/"rs:terrain_flow"/...)
  src/agent/spatial_tools/terrain_spatial_tools.* [NEW] spatial:terrain_profile, spatial:terrain_viewshed_inspect → kernels

I/O seams (existing, reused)
  processing/gdal/gdal_dataset_wrapper.h          read frames, geotransform, projection, nodata
  processing/gdal/gdal_multiband_block_stream.h   GdalStreamingOutput (atomic-ish create/write/closeWithError+abandon)
  processing/framework/resource_estimation.h      checkedMulN for memory estimates

Tests (independent oracles)
  tests/synthetic_raster_builder.h   file-level DEM builder for operator E2E
  tests/synthetic_terrain_dem.h [NEW] closed-form DEM factory: plane/cone/pit/ridge/channel
  tests/test_terrain*.cpp            Catch2 known-answer + E2E + negative + scale-bounded
```

## Failure semantics (family contract)

- NoData: declared sentinel, else NaN (operators normalize). NoData cells are
  barriers in hydrology (never filled/routed), nodata-passthrough in local kernels.
- Errors: `RSOperatorError(ErrorCode::…)` — InvalidParameter (bad params),
  InvalidInputData (empty/oversized DEM), GdalError (I/O), ComputationError,
  FileNotWritable; Cancelled via `context.throwIfCancelled()`.
- Determinism: fixed neighbour orders, deterministic tie-breaks documented in
  headers; operators pinned `bit_exact` in capability sidecars.
- Outputs: written via GdalStreamingOutput with `abandon()` on failure
  (no partial-file success), then `closeWithError` for finalize.

## This track's additions to the seam map

- `HorizonGrid` (terrain_viewshed.h): per-cell horizon angles in N azimuth
  sectors, produced by R3 sweep; consumed by viewshed classification AND solar
  shadow products (single LoS authority, DECISIONS D5/D6/D7).
- `resolveFlats(dem, filled, w, h, nodata, epsilonMode)` — same barrier
  semantics as fillDepressions; monotone non-decreasing; OFF by default in
  product=fill (byte-compat), ON as new product `flat_resolve`.
- Cell-count guard helper shared by full-frame operators (`terrain_max_cells`).
