# ADR 0130: Geospatial Data Interoperability & I/O Foundation 4.0

- Status: Accepted (I/O Foundation 4.0 goal series)
- Context: outside vendored QGIS core, geospatial I/O behavior was scattered across
  subsystems with divergent semantics — scale/offset was read nowhere and written
  nowhere; generic writers dropped band metadata (roles/wavelengths/color tables); four
  different WKT serialization variants coexisted with no axis-order policy; "atomicity"
  existed only inside OutputCommitter while several writers wrote direct-to-target (one
  of them deleting the previous good output on failure); COG was a filename guess in the
  STAC dialog; STAC was read-only and duplicated across GUI and headless parsers;
  multidimensional data had no semantics (subdatasets ad hoc); product metadata parsing
  (cloud cover, Sentinel-1) was incomplete; and "format support" was indistinguishable
  from "GDAL has a driver". Phase-0 audit: `.planning/geospatial-io-foundation-4/BASELINE.md`.
- Decision:
  1. **Qt-free core**: `src/geospatial` (library `Sicnu::Geospatial`) owns canonical
     metadata, CRS policy, raster/vector reader+writer contracts, the certified format
     registry, COG validator/presets, STAC mapping, the multidim view, product adapters
     and the data doctor. It depends only on C++20 std, GDAL/PROJ and jsoncpp — no Qt —
     so headless surfaces and tests consume it without the vendored QGIS chain.
  2. **One metadata vocabulary**: `RasterMetadata`/`VectorMetadata`/`MultidimMetadata`
     carry size/CRS/geotransform/band{nodata,scale,offset,unit,role,wavelength,color
     table}/overviews/compression/subdatasets/product semantics, with JSON round-trip.
     Inspection is one read-only open; statistics are an explicit opt-in (bounded).
  3. **Explicit CRS contract**: source/destination CRS and axis order are declared at
     the transform boundary (`CrsTransform::create` with `AxisOrder`); geotransform and
     extent coordinates are always traditional-GIS order. Missing dataset CRS is
     `MissingCrs` — guessing is forbidden; the only fallback is a caller-declared one
     (`CrsPolicy::allowDeclaredFallback`), surfaced as such in diagnostics.
  4. **Streaming with byte budgets**: window reads are the only bulk raster access;
     `readFull` requires an explicit byte budget and refuses overruns; vectors stream in
     bounded batches with attribute projection and driver-evaluated filters; 100k+
     features never materialize whole tables.
  5. **Atomic publish by construction**: writers stage beside the target, fsync,
     validate by reopening, then publish (single-file rename or dataset-group
     sidecars-first/main-last for shapefile families); cancel and destructor paths
     discard staging; failure tests cover cancel, locked targets, partial sidecars and
     injected write failures.
  6. **Certification ≠ driver presence**: `FormatRegistry` declares Certified /
     Accessible / Unsupported per format, where Certified means this repository carries
     round-trip tests proving the declared fidelity (GeoTIFF, COG, VRT, NetCDF-slice,
     GeoPackage, GeoJSON, Shapefile, STAC); runtime driver availability is resolved so
     the matrix never claims a format the current build cannot read.
  7. **COG as a first-class, validated product**: production goes through the COG
     driver with safe presets (`lossless_scientific`, `visualization` (lossy, explicit
     opt-in), `categorical` (lossless by policy, refuses float), `continuous_float`,
     `sar`), all pinned to `BIGTIFF=IF_SAFER`; `validateCog` audits tiling, overview
     pyramid and compression before publish.
  8. **Authoritative conversions through operators**: `io:translate`, `io:warp`,
     `io:reproject`, `io:clip`, `io:convert_format`, `io:build_overviews`,
     `io:make_cog`, `io:vector_convert`, `io:inspect`, `io:doctor` wrap the GDAL
     utility kernels — no resampling/reprojection math is re-implemented — and the CLI
     exposes the same layer as `data inspect|doctor` with the stable JSON envelope.
  9. **Product adapters are the single parser**: Landsat MTL, Sentinel-2 SAFE,
     Sentinel-1 manifest, MODIS and generic-GeoTIFF semantics (sensor/platform/
     acquisition/cloud/scaling/polarizations/orbit) parse once in `product_adapters`
     (expat, no Qt); algorithms enrich canonical metadata through it instead of
     re-parsing per consumer.
- Consequences:
  - Fidelity is testable: the round-trip matrix (GeoTIFF→GeoTIFF, GeoTIFF→COG→GeoTIFF,
    GeoPackage→GeoJSON→GeoPackage, GeoTIFF→VRT, NetCDF slice→GeoTIFF, STAC Item→
    canonical) pins CRS/extent/dims/dtype/NoData/scale-offset/metadata/values.
  - The legacy writers (GdalDatasetWrapper::createOutputTiff, writeGdalOutput,
    RsSegmentMap::toGeoTIFF, …) keep working but are now the documented *legacy* tier;
    new code goes through the foundation contracts or the io:* operators.
  - NoData/scale/offset remain explicit: stored values and physical values are distinct
    concepts; nothing applies scaling implicitly anywhere in the layer.
  - The Qt-free core unlocks a fast Windows test lane (13 `test_io_*` suites + a
    benchmark harness) that does not build vendored QGIS.
  - MCP gains the `io:` prefix in its algorithm allow-list; CLI gains the `data`
    command; the GUI keeps its existing open paths (format matrix documents the gap).
