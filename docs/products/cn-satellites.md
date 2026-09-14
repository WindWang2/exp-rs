# Chinese Satellite Products (GF · ZY-3 · ZY-1 02C · HJ CCD)

> Contract: ADR 0157 + ADR 0147 · Code: `src/geospatial/products/cn_product_metadata.*`
> (identity + CRESDA sidecar parsing), `src/geospatial/products/sensor_profile.*`
> (sensor profile registry loader), `src/geospatial/products/cn_product_adapters.cpp`
> (registry adapters), `src/operators/rs/rs_product_import_plan.*` (standardized
> import plan service), `src/operators/rs/rs_cn_product_import_operator.*`
> (unified operator) and `rs_gaofen_import_operator.*` / `rs_zy3_import_operator.*` /
> `rs_hj_import_operator.*` (family entry points).
> Sensor truth: `data/products/sensor_profiles/{gaofen,zy3,zy1,hj}.json`.

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
| GF-7 | `gaofen_product` | FWD, BWD | per camera: B1–B4 (MS) + pan | stereo mapping pair; each camera's PAN/MS resolved from the sidecar shape |
| ZY-3 | `zy3_product` | TLC/NAD/FWD/BWD | NAD MS B1–B4 (5.8 m); pan B1 (2.1 m); FWD/BWD pan (3.5 m) | CRESDA-style naming (`ZY3_NAD_…`, `ZY3_TLC_…`) |
| ZY-1 02C | `zy1_product` | PMS, HRC | PMS: B1–B4 (10 m) + pan (5 m); HRC: pan (2.36 m) | `ZY1_02C_PMS_*`, `ZY1_02C_HRC_*` |
| HJ-1A/1B | `hj_ccd_product` | CCD1/CCD2 | B1–B4 (30 m) | both satellites carry two identical CCD cameras |
| HJ-2A/B | `hj_ccd_product` | CCD | B1–B4 (16 m) | 02 batch CCD; HSI/AIS refused |

**Everything else is refused with a reason** — GF-3 (SAR), GF-4, GF-5,
ZY-1 02B/02D/02E (AHSI), ZY-5, CBERS and HJ-1 IRS / HJ-2 HSI are *recognized*
CN names that produce an `UnsupportedProduct` diagnosis, never a silent
generic-raster fallback and never a best-effort guess.

## Sensor profile registry (single band-truth authority)

`data/products/sensor_profiles/*.json` is the one place sensor layouts live:
platform, instrument, sensor mode, modality, nominal GSD, expected product
constituents (`sidecar_xml`, `image_tiff`, `rpc_rpb`), the declared
calibration rule, QA vocabulary, and the band layout (canonical role with a
mandatory `role_reason` for unknown roles, documented spectral-range
midpoint `wavelength_nm`, nominal `center_wavelength_nm` and `fwhm_nm` where
a published source states them, verbatim `spectral_range_um`, per-band GSD).
Pan/multispectral siblings are linked by `pan_variant`/`ms_variant`; the
sidecar's declared shape (ModeID or 1-band inventory) picks the variant.
The loader is fail-closed and version-gated; unknown registry keys are
ignored but reported as forward-compatibility warnings. The former
`band_roles/*.json` tables were folded into this registry (ADR 0147); the
spectral-resampling grids of `data/spectral/sensors.json` remain the SRF
authority for `Library::resampleTo` — midpoint vs centre are different
documented quantities, both now carried explicitly.

## Sidecar generations (explicit, diagnosable)

Every parse reports `parseDiagnostics`: the detected generation
(`cresda_legacy_metainfo` for `<MetaInfo>`, `cresda_current_metadata` for
`<ProductMetaData>`, `cresda_unknown_root` for anything else the whitelisted
tags still match), the root element, and the top-level elements the parser
did **not** consume (bounded report). Import results carry
`sidecarGeneration`, `sidecarRoot` and `warnings[]` — a future CRESDA
generation degrades nothing silently.

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

## Band roles (ADR 0065) come from the registry

Sensor profiles (see above) are loaded at runtime (SICNU_DATA_DIR → walk-up →
compiled source dir; missing registry = structured error, version mismatch =
typed error). Roles use the canonical vocabulary; `wavelength_nm` values are
documented range midpoints and `center_wavelength_nm`/`fwhm_nm` appear only
where published sources state them. A range width is never a FWHM.

Special cases, explicit not guessed:

* GF PMS / GF-7 / ZY-1 02C **pan** files and MS files share the `BandID`
  letters; the registry `pan_variant` link plus the sidecar's declared shape
  (`ModeID`=PAN or a 1-band inventory) decides the variant
  (`gf*_pms_pan`, `gf7_fwd_pan`, `zy1_02c_pms_pan`, ...).
* ZY-3 NAD pan vs MS is resolved the same way (1 declared band → `zy3_pan`).
* GF-6 WFV B7 (violet) and B8 (yellow) have **no** canonical ADR 0065 role —
  they carry `unknown` with the reason attached, in the table and on every
  enumerated asset.

## Import: one service, four entry points

All ingestion runs through the standardized import-plan service
(`rs_product_import_plan`): identify → inspect → validate → resolve
constituents → role map → optional calibration → stack → stamp → provenance.

| Operator | Scope | Default bands |
| --- | --- | --- |
| `rs:cn_product_import` | any supported CN family (auto-identified) | all bands declared by the sidecar |
| `rs:gaofen_import` | GF-1/2/6 PMS/WFV + GF-7 FWD/BWD | same (MSS preferred for PMS directories) |
| `rs:zy3_import` | ZY-3 TLC/NAD/FWD/BWD | same |
| `rs:hj_import` | HJ-1A/1B CCD + HJ-2A/B CCD | same |

Input may be the product directory, the sidecar XML or the image TIFF.
Output: stacked Float32 GeoTIFF with, per band, `SICNU_BAND_ROLE`,
`WAVELENGTH`/`WAVELENGTH_UNITS` and declared `SICNU_CALIB_GAIN_<band>` /
`SICNU_CALIB_BIAS_<band>`; dataset-level `SICNU_PRODUCT_TYPE`
(`gaofen_product` / `zy3_product` / `zy1_product` / `hj_ccd_product`),
`SICNU_PRODUCT_FAMILY=cn`, `SICNU_SENSOR`, `SICNU_SENSOR_MODE`,
`SICNU_SPACECRAFT`, `SICNU_PROCESSING_LEVEL`, `SICNU_ACQUISITION_DATE`,
`SICNU_ORBIT_ID`, `SICNU_SUN_ELEVATION_DEG`, `SICNU_SUN_AZIMUTH_DEG`,
`SICNU_RADIOMETRIC_STATE`.

The result carries the legacy keys (`declared{}` flags,
`missingDeclaredFields[]`, `bandSource`, `bandRoles`) plus the ADR 0147
provenance: `sensorProfile` + `sensorProfileVersion`, `sidecarGeneration`,
`sidecarRoot`, `completeness` (`complete` / `partial_readable`),
`missingConstituents[]`, `warnings[]`, `declared.rpc`, `bandWavelengthsNm[]`,
`rpc`, `siblingImage`/`siblingRole` (the PMS pair's other half, role-tagged)
and `calibration{}`. Agents can therefore tell "the product does not declare
sun elevation" from "sun elevation is 0", and "this product is incomplete"
from "this product refused".

### Optional declared-coefficient calibration

`apply_calibration=true` applies the sidecar's declared coefficients as
`radiance = DN × gain + bias` to every requested band and stamps
`SICNU_RADIOMETRIC_STATE=radiance`. When any requested band lacks both
coefficients the import refuses with a typed error naming the bands —
never a partially calibrated stack, never a defaulted coefficient.

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

`tests/test_cn_products.cpp` — per-family identity/refusal (incl. GF-7,
ZY-1 02C, HJ-2 CCD and their refused neighbours), CRESDA parsing (declared +
explicitly-absent fields), calibration shapes (flat lists and per-band
elements) and application (DN→radiance, typed partial-coverage refusal),
sensor profile registry (authority, schema gate, forward-compat reporting),
sidecar generation detection with unknown-element diagnostics, RPC/PMS-sibling
constituent resolution, all four import operators (incl. unified
`rs:cn_product_import`), Chinese-path encoding and the headless
NDVI/classification chain.
