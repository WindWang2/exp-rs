# Canonical Metadata Model

One in-memory vocabulary (`src/geospatial/metadata/canonical_metadata.h`)
that every adapter maps into and out of. JSON serialization is versioned
(`format_version: 1`) and round-trip tested (`test_io_canonical_metadata`).

## Inspection contract

```cpp
using namespace sicnu::geo;
RasterMetadata meta = inspectRaster("scene.tif");   // one read-only open
VectorMetadata vec  = inspectVector("parcel.gpkg"); // layer schema, cheap counts
MultidimMetadata md = inspectMultidim("cube.nc");   // root group walk, lazy
Json::Value any     = inspectAny("dataset");        // raster → vector → multidim
```

Guarantees:

- **No full scan**: inspection reads driver metadata only. Per-band statistics
  are an explicit opt-in (`InspectOptions::includeStatistics`, bounded to a
  512×512 decimated read, mirroring `spatial:raster_inspect`).
- **Absence is explicit**: every optional field carries a `has*` flag
  (`hasNoData`, `hasScale`, `hasExtent`, …). Nothing fabricates defaults.
- **Bounded output**: dataset/band metadata capture and subdataset listing are
  capped (`maxMetadataItems`, `maxSubdatasets`).
- **Product semantics ride declared stamps** (`SICNU_SENSOR`,
  `SICNU_RADIOMETRIC_STATE`, `SICNU_NUMERIC_SCALE`, wavelength/FWHM, …) with
  conservative standard-key fallbacks — never invented.

## Raster fields

`path · driver · width · height · bandCount · crs{wkt, authid, geographic,
projected, coordinateEpoch} · geotransform · extent(minX,minY,maxX,maxY) ·
resolution · bands[{dtype, description, nodata, scale, offset, unit, role,
wavelengthNm, fwhmNm, colorInterpretation, colorTable, maskFlag, metadata}] ·
overviewCount · compression · interleave · subdatasets · gcps/rpc flags ·
sensor · platform · productId · processingLevel · acquisitionTime ·
radiometricState · numericScale · cloudCover · gsd · metadata`.

Vector: per layer `geometryType · featureCount (+exact flag) · crs · fields ·
extent (+exact flag) · encoding · filter/write capabilities`.

Multidimensional: `dimensions[{name, size, type, direction, unit}] ·
variables[{name, dtype, dims, unit, nodata, scale, offset, attributes}]`.

## JSON example

```json
{
  "format_version": 1,
  "kind": "raster",
  "driver": "GTiff",
  "width": 8, "height": 6, "band_count": 3,
  "crs": { "valid": true, "authid": "EPSG:4326", "is_geographic": true },
  "has_geotransform": true,
  "extent": [100.0, 37.0, 104.0, 40.0],
  "bands": [ { "index": 1, "dtype": "Float32", "has_nodata": true,
               "nodata": -9999.0, "has_scale": true, "scale": 0.0001,
               "role": "Blue", "wavelength_nm": 490.0 } ],
  "radiometric_state": "toa_reflectance"
}
```

`nodata` serializes as the string `"nan"` for NaN sentinels (JSON has no NaN).
