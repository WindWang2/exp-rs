# ARCHITECTURE — Scientific Processing 8.0

## WP A — `rs:sar_geocode`: full Range-Doppler geocoding / terrain chain

Predecessor: 7.0's `sar_orbit` machinery (orbit parse/validate/Hermite,
zero-Doppler geolocation, forward range-Doppler) and the backward
`local_incidence_orbit` product. 7.0 explicitly refused forward output-grid
geocoding; this track implements it as a new kernel + operator, reusing the
orbit seam unchanged.

### Kernel: `src/processing/algorithms/sar/sar_geocoding.{h,cpp}`

Chain per output map-grid pixel (grid defined by the DEM raster — the DEM is
the geocoding target grid, the established convention for RTC products):

1. DEM pixel center → geodetic (lon/lat) via the DEM geotransform; height =
   DEM sample (NoData handling: pixel is NoData, never interpolated garbage).
2. geodetic → ECEF (`Wgs84::geodeticToEcef`).
3. `forwardRangeDoppler(orbit, p)` → azimuth time + slant range. Unresolvable
   (outside orbit segment / no zero-Doppler crossing) → NoData pixel with a
   per-product unresolvable counter.
4. Map azimuth time → image row: `(t − AZIMUTH_START_UTC) · PRF`; slant range
   → image column via the two-way range sampling
   `col = (2·R/c − RANGE_WINDOW_START) · RANGE_RATE` — the same formulas the
   backward product uses, inverted. Row/col outside the SAR image → NoData.
5. Bounded resampling of SAR radiometry at the floating-point source position
   (bilinear; declared `nearest` option). Non-finite source → NoData.
6. Local incidence from the REAL line of sight (satellite position from orbit
   interpolation at the azimuth time; surface normal from DEM Horn gradients
   with a 1-pixel halo — shared with `rs:sar_terrain_masks`).
7. Geometric classes: layover/shadow computed on the map grid — layover when
   the local incidence angle is negative-equivalent (slope faces the sensor
   beyond the LOS); shadow when the LOS elevation angle is below the terrain
   slope away from the sensor; both derived from the same per-pixel vectors,
   no approximations beyond the DEM's own discretization.
8. Terrain/area factor (sigma0 → gamma0 RTC): per-pixel projected-area
   factor = sin(θ_i_local_real) / sin(θ_i_ref) using the REAL local
   incidence angle and the reference (ellipsoidal) incidence at that pixel —
   i.e. the standard area-gain formulation evaluated with real geometry, not
   the plane-fit proxy. Applied multiplicatively to sigma0 radiometry.
9. Products (one run, one or more outputs):
   - `sigma0` geocoded radiometry (nearest/bilinear resample)
   - `gamma0` RTC (sigma0 × area factor)
   - `incidence` (real per-pixel, degrees)
   - `layover_shadow` (classes 0/1/2, same vocabulary as `rs:sar_terrain_masks`)
   - `dem` (resampled height actually used — provenance/debug)
10. Refusals (typed, no approximation): undeclared/invalid orbit contract;
    SAR image and DEM without geodetic CRS definitions; PRF/range contract
    contradictions; DEM NoData grid empty. Non-georeferenced SAR or DEM →
    typed error.

Memory: streaming by output row-block (tile height bounded, e.g. 256 rows ×
width window; DEM halo reads through `GdalDatasetWrapper` windows; SAR source
read per-block via RasterIO on the computed source window only). O(tile) —
no full-frame buffers. Progress + cancellation through the standard operator
context seam (checked between row blocks).

Determinism: bit-exact grade (no parallel reduction over floats; pure
per-pixel math; fixed iteration bounds in the bisection solver inherited from
`sar_orbit.cpp`).

### Known-answer tests (synthetic, analytic)

- **Circular orbit + spherical-cap DEM ("flat ellipse")**: DEM = constant
  height h above WGS84. Forward-geocoding the DEM grid must land every
  output pixel back on the SAR image position whose backward geolocation
  round-trips to within a tight tolerance (closed loop backward∘forward).
- **Round-trip identity**: for random DEM heights (seeded), backward
  geolocation of (row,col) then forward range-Doppler of the result must
  recover (row,col) within sub-pixel error.
- **Area factor known answer**: flat DEM → real local incidence == ellipsoid
  incidence → factor = 1; tilted-plane DEM → analytic sin ratio verified
  against closed form.
- **Refusals**: missing orbit keys, invalid segment, non-georeferenced DEM,
  empty-orbit intersection (range too short) — typed errors, no output.
- **NoData semantics**: DEM NoData region, out-of-swath columns, SAR NoData.
- **Operator E2E**: synthetic GeoTIFF DEM + SAR + metadata keys → products
  on disk, metadata band semantics, help/catalog alignment.

### Operator: `rs:sar_geocode` (`src/operators/rs/rs_sar_geocode_operator.{h,cpp}`)

Params: `input` (SAR raster), `dem`, `output` (+ per-product outputs),
`resampling` (nearest|bilinear), `radiometry` (sigma0|gamma0), `products`
(array). Metadata contract: orbit keys read from the SAR dataset (declared
scene metadata), DEM supplies the target grid + CRS. Results report
per-product paths, unresolvable/NoData fractions, effective contract values.

## WP B — `rs:sar_temporal_stats`: multi-date SAR statistics

Kernel: `src/processing/algorithms/sar/sar_temporal.{h,cpp}`,
streaming per-pixel over N co-registered scenes (linear power domain enforced
by the existing SAR domain contract; dB inputs converted once):

- mean/stddev/CV over valid (finite, positive-domain-checked) linear power
- dB reporting: 10·log10(mean) with the linear-domain aggregate documented
- min/max + argmax/argmin date indices
- robust log-domain change: per-pixel log-median baseline; output
  max |10·log10(x_i) − baseline| and mean log deviation (robust change
  magnitude) — operates in the dB domain where multiplicative speckle is
  additive
- equivalent look count / dispersion metric (spectral dispersion proxy:
  variance/mean² per-channel normalized)
- valid_count / date masks; scene-level valid fraction reported

Operator: `rs:sar_temporal_stats` (`rs_sar_temporal_stats_operator.{h,cpp}`).
Refusals: <2 scenes, grid mismatch, undeclared mixed domains without
explicit override, non-SAR band roles (warning path documented).

## WP D — raster-vector operators

### Kernel: `src/processing/algorithms/raster_vector.{h,cpp}` (GUI-free)

- `burnVector(in GeoTIFF grid, VectorReader zones, value mode)`: for each
  streamed feature, parse WKT via OGR (OGRGeometryFactory::createFromWkt —
  the geospatial VectorReader already produces WKT), rasterize per output
  row-block with GDALRasterizeGeometries on bounded MEM windows
  (window = row block + no halo; geometry coordinates transformed into raster
  pixel space via the geotransform). All-touched vs pixel-center semantics
  follow GDAL's options. Values: constant burn or an attribute field
  (numeric; non-numeric → typed refusal). Later features overwrite earlier
  (documented LAST-WINS), with a `overwrite=false` variant that skips
  already-burned pixels per window.
- `zonalStats(value raster, VectorReader zones, stats...)`: for each feature,
  rasterize its geometry into the row-block window (pixel-center mask), read
  the intersecting value window once per block per feature-batch and
  accumulate per-zone: count, min, max, mean, stddev (population),
  median (exact, bounded reservoir-free — per-zone value collection with a
  hard per-zone cap and typed refusal/overflow flag above it), percentile(s).
  Zones identified by FID or an attribute key. Streaming both directions:
  features stream in batches; rasters stream in row blocks; a zone may span
  blocks (accumulator lives across blocks). Memory bound: O(active zones ×
  per-zone state) + one row-block window; per-zone bounded, documented,
  refuses (or flags) beyond the cap.
- CRS handling: VectorReader's `setTargetCrs` transforms feature WKT into the
  value raster's CRS (foundation axis-order policy) before rasterization.
  No CRS declared on either side → typed refusal (no silent same-CRS guess).

### Operators

- `rs:rasterize`: params `input` (reference raster grid), `vector`, `field`
  or `value`, `allTouched`, `overwrite`, `output`. Result: path + burned
  fraction.
- `rs:zonal_stats`: params `input` (value raster), `vector` (zones), `zoneField`,
  `bands`, `stats` (array), `output` (CSV) + JSON summary in result. Result:
  per-zone table + zone count.

Both reuse the SAME rasterization seam (one kernel, two entry points) so
pixel-membership semantics cannot drift between them.

## WP E — spectral formula drift guard

New test `tests/test_spectral_formula_drift.cpp`: a single table binds
each documented index (ndvi, evi, savi, ndwi, ndbi, mndwi, gndvi, arvi, ui,
bui, evi2, msavi, bai, ndmi...) to
(a) the kernel function pointer and (b) numeric probes with pinned expected
values derived from the formula in the header docs, evaluated at
unit-reflectance and DN-scale regimes. The same table emits the formula
string into the test log and cross-checks `AlgorithmHelpCatalog`/sidecar
descriptions mentioning formulas where present. Adding an index without a
table row, or changing a constant without updating the pinned probe, fails
the build's test suite — mechanical drift detection.

## WP C — temporal family

Audit verdict: master already implements the full family with one time-axis
contract (docs/processing/temporal.md). This track adds NO second
implementation. Action: extend TEST evidence where the audit finds holes
(discovered during review) and document the verdict in
CAPABILITY_MATRIX.md. `rs:sar_temporal_stats` (WP B) is the SAR-domain
addition that plugs into the same missing/quality-mask vocabulary.

## WP F/G/H — audit-only verdicts

F (terrain/hydrology), G (classification), H (contract layer): implemented
with documented contracts; this track audits for contract violations during
the adversarial review and records the verdict; no speculative new
capabilities.
