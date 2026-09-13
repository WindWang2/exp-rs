# Chinese Satellite Products (GF-1/2/6 · ZY-3 · HJ-1A/1B CCD)

> Contract: ADR 0146 · Code: `src/geospatial/products/cn_product_metadata.*`
> (identity + CRESDA sidecar parsing + band-role tables),
> `src/geospatial/products/cn_product_adapters.cpp` (registry adapters),
> `src/operators/rs/rs_gaofen_import_operator.*` / `rs_zy3_import_operator.*` /
> `rs_hj_import_operator.*` (import operators).
> Band-role tables: `data/products/band_roles/{gaofen,zy3,hj}.json`.

The mainstay data of Chinese undergraduate remote-sensing teaching is not
Landsat — it is 高分 (GF-1/2/6), 资源三号 (ZY-3) and 环境减灾 (HJ-1A/1B CCD).
These products are **first-class citizens** here: recognized, parsed offline,
role-tagged and imported with calibration/sun-geometry metadata intact.
Previously they all degraded to `GenericRaster` and every downstream step
needed hand-typed band numbers and coefficients.

## Supported products (fixed set)

| Family | Kind / adapter | Sensors | Bands | Notes |
| --- | --- | --- | --- | --- |
| GF-1 | `gaofen_product` | PMS1/PMS2, WFV1–4 | PMS: B1–B4 (MS, 8 m) + B1 pan (2 m); WFV: B1–B4 (16 m) | `-MSS1.xml` / `-PAN1.xml` sidecars |
| GF-2 | `gaofen_product` | PMS1/PMS2 | B1–B4 (MS) + pan | 1 m pan / 4 m MS class |
| GF-6 | `gaofen_product` | PMS, WFV | PMS: B1–B4 + pan; WFV: **B1–B8** (16 m) | WFV adds red-edge ×2, violet, yellow |
| ZY-3 | `zy3_product` | TLC/NAD/FWD/BWD | NAD MS B1–B4 (5.8 m); pan B1 (2.1 m) | CRESDA-style naming (`ZY3_NAD_…`, `ZY3_TLC_…`) |
| HJ-1A/1B | `hj_ccd_product` | CCD1/CCD2 | B1–B4 (30 m) | both satellites carry two identical CCD cameras |

**Everything else is refused with a reason** — GF-3 (SAR), GF-4, GF-5, GF-7,
ZY-1/ZY-5, HJ-2, CBERS and HJ-1 IRS are *recognized* CN names that produce a
`UnsupportedProduct` diagnosis, never a silent generic-raster fallback and
never a best-effort guess.

## What gets parsed (offline, from the product's own sidecar)

The CRESDA-family L1A sidecar XML (`<MetaInfo>…`) is the authoritative
source. Parsed fields: `ProductID`, `SatelliteID`, `SensorID`, `ModeID`,
`ReceiveDate`+`ReceiveTime` (→ ISO-8601), `CloudPercent`, `PixelSizeX`,
`OrbitID`, `SunPosGeodetic::Azimuth/Elevation`, the repeated `BandID`
inventory, and — when the batch declares them — `GainVal`/`OffsetVal` or
per-band `BandCalibration::Gain/Offset/Bias`.

**Calibration convention.** Carried coefficients follow the CRESDA
`GainVal`/`OffsetVal` semantics: **radiance = DN × GAIN + BIAS**, i.e.
`SICNU_CALIB_GAIN_<band>` multiplies and `SICNU_CALIB_BIAS_<band>` is the
additive term. Values are stamped verbatim to 10 significant digits; the
platform namespace keys are additionally mirrored as plain `SUN_ELEVATION`
(degrees above horizon, Landsat-MTL convention) so the radiometric-calibration
and DOS flows can consume declared sun geometry without changes.

**Absence is absence.** Batches whose sidecar does not carry calibration
coefficients (most older CRESDA distributions publish per-date coefficients
separately) report `band_calibration` in `missingDeclaredFields`; the
radiometric state is stamped `digital_number` and the classroom workflow
(look up the published coefficient table for the acquisition date) is
described in the radiometric-calibration lab flow. No coefficient is ever
invented, defaulted or fetched.

Sun geometry note: `SunPosGeodetic::Elevation` is the sun **elevation** above
horizon (degrees) — stored as `SICNU_SUN_ELEVATION_DEG`. Consumers needing
zenith compute `90 − elevation` themselves.

## Band roles (ADR 0065) are data-driven

`data/products/band_roles/*.json` is the single source of truth, loaded at
runtime (SICNU_DATA_DIR → walk-up → compiled source dir; missing table =
structured error). Roles use the canonical vocabulary; wavelengths are
documented range midpoints (the JSON carries the ranges as notes). FWHM is
deliberately **not** stated — a range width is not a FWHM.

Special cases, explicit not guessed:

* GF PMS **pan** files and MS files share the `BandID` letters; the sidecar
  `ModeID` (PAN/MSS) decides the table (`gf*_pms_pan` vs `gf*_pms`).
* ZY-3 NAD pan vs MS is resolved from the declared band inventory (1 band →
  pan table, ≥2 → multispectral).
* GF-6 WFV B7 (violet) and B8 (yellow) have **no** canonical ADR 0065 role —
  they carry `unknown` with the reason attached, in the table and on every
  enumerated asset.

## Import operators

| Operator | Family | Default bands |
| --- | --- | --- |
| `rs:gaofen_import` | GF-1/2/6 PMS/WFV | all bands declared by the sidecar (MSS preferred for PMS directories) |
| `rs:zy3_import` | ZY-3 TLC/NAD/FWD/BWD | same |
| `rs:hj_import` | HJ-1A/1B CCD | same |

Input may be the product directory, the sidecar XML or the image TIFF.
Output: stacked Float32 GeoTIFF with, per band, `SICNU_BAND_ROLE`,
`WAVELENGTH`/`WAVELENGTH_UNITS` and declared `SICNU_CALIB_GAIN_<band>` /
`SICNU_CALIB_BIAS_<band>`; dataset-level `SICNU_PRODUCT_TYPE`
(`gaofen_product` / `zy3_product` / `hj_ccd_product`), `SICNU_PRODUCT_FAMILY=cn`,
`SICNU_SENSOR`, `SICNU_SENSOR_MODE`, `SICNU_SPACECRAFT`, `SICNU_PROCESSING_LEVEL`,
`SICNU_ACQUISITION_DATE`, `SICNU_ORBIT_ID`, `SICNU_SUN_ELEVATION_DEG`,
`SICNU_SUN_AZIMUTH_DEG`, `SICNU_RADIOMETRIC_STATE=digital_number`.

The operator result also carries `declared{}` flags and
`missingDeclaredFields[]` so agents can tell "the product does not declare
sun elevation" from "sun elevation is 0".

## Field map (sidecar tag → where it lands)

| CRESDA tag | `ProductMetadata` field | Output metadata |
| --- | --- | --- |
| `ProductID` | `productId` | `SICNU_PRODUCT_ID` |
| `SatelliteID` | `platform` | `SICNU_SPACECRAFT` |
| `SensorID` | `sensor`, `sensorMode` | `SICNU_SENSOR`, `SICNU_SENSOR_MODE` |
| `ModeID` | `extra["mode_id"]` (pan/MS table selection) | — |
| `ReceiveDate`+`ReceiveTime` | `acquisitionTime` (ISO) | `SICNU_ACQUISITION_DATE` |
| `CloudPercent` | `cloudCover` (+`hasCloudCover`) | — |
| `PixelSizeX` | `resolutionMeters` (+`hasResolution`) | — |
| `OrbitID` | `orbitId` | `SICNU_ORBIT_ID` |
| `SunPosGeodetic::Elevation/Azimuth` | `sunElevationDeg`/`sunAzimuthDeg` | `SICNU_SUN_ELEVATION_DEG`/`SICNU_SUN_AZIMUTH_DEG` |
| `BandID` (repeated) | `declaredBandIds` (TIFF band order) | per-band assets / stack |
| `GainVal`/`OffsetVal`, `BandCalibration::Gain/Offset/Bias` | `bandCalibration[]` | `SICNU_CALIB_GAIN_<band>` / `SICNU_CALIB_BIAS_<band>` |

## Teaching flow (headless)

```
rs:gaofen_import  →  rs:spectral_index (NDVI, roles resolve red/nir)
                  →  rs:supervised_classification (training polygons)
```

The e2e chain runs headless in `tests/test_cn_products.cpp`
(`GF-1 headless end-to-end`) with a fully synthetic product; no real imagery
is committed anywhere.

## Tests

`tests/test_cn_products.cpp` — per-family identity/refusal, CRESDA parsing
(declared + explicitly-absent fields), calibration shapes (flat lists and
per-band elements), band-role tables (incl. GF-6 WFV unknown-role reasons),
all three import operators, and the headless NDVI/classification chain.
