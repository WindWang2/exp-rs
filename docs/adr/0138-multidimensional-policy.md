# ADR 0138: Multidimensional Data Policy (5.0)

- Status: Accepted (Remote Sensing I/O Foundation 5.0 goal)
- Context: NetCDF/HDF/Zarr stores are multidimensional; the historical path
  flattened subdatasets into raster bands, which fabricates semantics (a
  time×y×x cube becomes N unrelated "bands"). GDAL's multidim API (MDArray)
  is available in the vcpkg build for netCDF and HDF5; Zarr arrives through
  GDAL's Zarr driver when present.
- Decision:
  1. **No forced flattening**: `MultidimView` addresses variables and slices
     dimensions *by name*; a slice must leave exactly two free (spatial)
     dimensions. Flattening cubes into pseudo-bands is a fidelity violation
     and is not offered by the layer.
  2. **Lazy metadata**: inspection enumerates groups/dimensions/variables/
     attributes without reading array data; reads go through bounded slices
     (`maxCells` budget, default 64 Mi cells).
  3. **CF honesty**: coordinate axes are read from the indexing variable when
     declared (units, calendar, values). A time axis without a readable
     coordinate variable is reported as *unlabeled indices* — the layer never
     invents timestamps. Grid mapping (CF `grid_mapping`) is mapped onto the
     canonical `CrsInfo` when parseable; absent CRS stays absent.
  4. **Capability honesty**: multidim support is driver-gated at runtime.
     A build without the netCDF/HDF5/Zarr driver reports
     `GeoError::DriverMissing` / capability `multidim=false` — profiles
     degrade to explicit `Unsupported`, never to a silent half-support.
  5. **Zarr & GeoParquet**: no new heavyweight dependency is introduced in
     this track. Both are exposed through the capability model when the loaded
     GDAL carries the driver, with explicit unsupported diagnostics otherwise
     (audited in `docs/interoperability/format-matrix.md`).
- Consequences: time/level slicing is correct-by-construction; the temporal
  workspace and algorithms can key on variable semantics; unsupported builds
  fail loudly at capability-query time instead of at pixel-read time.
