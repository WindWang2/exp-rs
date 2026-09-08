# Sensor Product Support Matrix (Foundation 5.0)

> Verified through `tests/test_io_products.cpp` (metadata readers) and
> `tests/test_io_product_registry.cpp` (enumeration + completeness), plus the
> legacy `test_satellite_products.cpp` import flow. "identify/metadata" here
> means *tested on synthetic fixtures generated in this repository* — never
> "theoretically parseable".

| Product | Identify | Metadata normalized | Asset enumeration | Band roles | Time | QA assets | Completeness verdicts | Limitations |
|---|---|---|---|---|---|---|---|---|
| Landsat Collection 1/2 (MTL) | ● | platform/sensor/level/date/radiometric keys; rescaling constants (REFLECTANCE/RADIANCE MULT/ADD) reported as declared values | ● (MTL-declared FILE_NAME_* inventory) | ● (OLI/OLI_TIRS and legacy TM/ETM+ tables selected by SENSOR_ID; thermal ST_*, QA masks) | ● | ● (QA_PIXEL/QA_RADSAT/MSK_*) | ● (Partial/UnsupportedVersion for unknown COLLECTION_NUMBER) | scenes without MTL fall back to generic raster; unknown SENSOR_ID defaults to the OLI layout |
| Sentinel-2 SAFE (L1C/L2A) | ● | platform/level from filename heuristic; level, quantification and cloud cover from MTD when declared | ● (granule-bounded, R10m/R20m/R60m groups distinct) | ● (B01–B12/B8A optical roles + wavelengths; SCL/MSK as masks; AOT/WVP/TCI carry no optical role) | ● | ● (SCL/MSK as mask assets) | ● (manifest/MTD/measurements/core-band B02 checks) | multi-granule enumeration bounded at 16 |
| Sentinel-1 SAFE (GRD/SLC) | ● | platform/orbit direction/instrument mode/polarizations/modality sar | ● (measurement tiffs, annotation XML, calibration references) | ● (vv/vh/hh/hv role vocabulary) | ● (manifest start/stop) | ◐ (annotation presence feeds completeness) | ● (no measurements → Invalid; no annotations → PartialReadable) | calibration math itself is Track A |
| MODIS (HDF4/HDF5 container) | ● | platform from the MOD/MYD prefix (Terra/Aqua); processing level stays absent unless declared | ● (GDAL subdatasets, bounded at 64) | ◐ (subdataset naming; roles via MODIS band tables for known products) | ◐ (tile indices h/v parsed from names — georeferencing, not time) | ◐ | ● (unopenable container → Invalid; zero subdatasets → PartialReadable) | sinusoidal georeferencing flows live in the processing import path |
| Generic raster | ● (always claims) | — (canonical metadata only) | ● (single measurement asset) | ○ | ○ | ○ | ● (GDAL-identifiable or Invalid) | never reports a fake product family |

## Column contract

* **identify**: cheap, read-only claim check (path shape + targeted marker
  existence; no GDAL opens during accept).
* **metadata normalized**: mapped into `ProductMetadata` (ADR 0137) with raw
  source metadata preserved beside the normalized fields.
* **completeness**: `Complete` / `PartialReadable` / `Invalid` /
  `UnsupportedVersion` — see `docs/products/product-adapters.md`.
