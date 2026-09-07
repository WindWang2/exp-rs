# Sensor Product Adapters (Foundation 5.0)

> Contract: ADR 0137 · Code: `src/geospatial/products/product_registry.*`
> (registry + enumeration), `src/geospatial/products/product_adapters.*`
> (metadata readers).

Adapters *understand products*; pixel I/O stays delegated to GDAL. The
registry answers three questions per product path:

1. **which family claims it** — Landsat MTL, Sentinel-2 SAFE, Sentinel-1 SAFE,
   MODIS container, or the GenericRaster fallback that always claims
   openable rasters;
2. **what constituents it carries** — logical assets with role
   (`measurement` / `mask` / `annotation` / `metadata` / `browse`), native
   band name, canonical band role (lowercase vocabulary of
   `src/data/band_role.h`, extended with SAR `vv`/`vh`/`hh`/`hv`), center
   wavelength and declared resolution where the product declares them;
3. **is the product complete** — `Complete`, `PartialReadable` (core metadata
   readable, some constituents missing — the exact names are listed),
   `Invalid` (core metadata missing/unparseable), `UnsupportedVersion`
   (declared product version this codebase does not understand — never
   silently parsed).

## Policies

* Nothing is fabricated. A value the product does not declare stays absent
  (`has*` flags / empty). Filename heuristics are hints only; authoritative
  sidecars (MTL, MTD_*.xml, manifest.safe) win.
* Sentinel-2 resolution groups (R10m/R20m/R60m) remain **distinct** assets —
  the adapter never resamples 10 m bands onto the 20 m grid.
* Enumeration is bounded (≤16 granules, ≤64 subdatasets, ≤4096 entries per
  directory) and targeted (marker-file existence checks, no full scans).
* Sidecar-only inputs (MTL.txt, manifest.safe, a .SAFE directory) probe as
  products even when no GDAL driver can identify them.

## CLI

```
sicnu_geo_rs_cli data product describe <path>      # normalized metadata + assets + completeness
```

## Tests

`tests/test_io_product_registry.cpp` — synthetic MTL scenes (complete,
partial, unsupported-version), Sentinel-2 multi-resolution granules,
Sentinel-1 measurement/annotation/calibration enumeration, generic-raster
fallback, verdict name stability.
