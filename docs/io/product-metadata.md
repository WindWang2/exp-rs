# Product Metadata Adapters

One canonical parser for RS product sidecars (`src/geospatial/products/
product_adapters.h`): algorithms, operators and tools enrich canonical
metadata through it instead of re-parsing per consumer.

| Product | Source | Extracted |
|---|---|---|
| Landsat (Collection 1/2) | `*_MTL.txt` | productId, spacecraft, sensor, acquisition date+time, cloud cover, projection hint, radiometric state (reflectance mult present → `toa_reflectance`) |
| Sentinel-2 SAFE | `MTD_MSIL1C/2A.xml` | platform (S2A/B/C), level (1C/2A), `START_TIME`, cloud coverage, quantification (10000), radiometric state (TOA/BOA reflectance) |
| Sentinel-1 | `manifest.safe` | platform, instrument mode (IW/EW/SM), product type (GRD/SLC), polarizations (VV/VH/HH/HV, ordered), orbit direction, start time; `radiometricState=digital_number`, modality `sar` |
| MODIS | HDF container (via GDAL) | sensor, platform family, level |
| Generic GeoTIFF | GDAL metadata | `SICNU_*` stamps only (no invention) |

## Policies

- **Declared-only**: everything copied verbatim from the sidecar; absence
  stays absence. The adapters never invent a CRS or an acquisition time
  (`crsHint` is the product's *declared* projection text, not a resolution).
- **Fallback layer**: `enrichWithProductMetadata` only fills canonical fields
  that are still empty — explicit inline values always win.
- **Fail closed**: malformed XML and unreadable sidecars surface as
  `GeoError` (`OpenFailed` / `InvalidArgument`), never as silently
  half-populated products.

## Band roles and wavelengths

`productBandRole(kind, "B8A")` and `productBandWavelengthNm(kind, "B4", …)`
map product band names onto the canonical role vocabulary
(`src/data/band_role.h`): Sentinel-2 B01–B12 (coastal→SWIR2, narrow-NIR,
cirrus) and Landsat 8/9 B1–B11 (coastal→thermal). Unknown names return
empty/false — the caller decides whether that is an error for its context.

## Tests

`tests/test_io_products.cpp` builds synthetic MTL / SAFE / manifest fixtures
at runtime and pins every extracted field, including the structured failures.
