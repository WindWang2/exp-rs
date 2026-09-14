# ADR 0157: Chinese Satellite Product Adapters (GF / ZY-3 / HJ-1 CCD)

## Context

The platform's product layer (ADR 0137) knew five families: Landsat MTL,
Sentinel-2/1 SAFE, MODIS containers and a GenericRaster fallback. The
workhorse data of Chinese undergraduate teaching — GF-1/2/6, ZY-3,
HJ-1A/1B CCD — fell through to `GenericRaster`: no product id, no band
roles, no wavelengths, no sun geometry, no calibration coefficients. Every
NDVI/classification exercise started with hand-typed band numbers, and
radiometric steps were unusable headless.

The CRESDA-family L1A distributions are offline-friendly (sidecar XML +
TIFF, sometimes `.rpb` RPC), which matches the classroom hard requirement:
no network access during labs.

## Decisions

1. **Fixed support set, diagnosable refusal.** GF-1/2/6 (PMS/WFV),
   ZY-3 (TLC/NAD/FWD/BWD), HJ-1A/1B (CCD) are adapted. Other CN names
   (GF-3/4/5/7, ZY-1/5, HJ-2, CBERS, HJ-1 IRS) are *recognized* and refused
   with a concrete reason (`UnsupportedProduct` + details) — never a silent
   GenericRaster degradation, never best-effort guessing. `ProductKind`
   gains `GaofenProduct`, `Zy3Product`, `HjCcdProduct`.

2. **Offline parsing from the product's own sidecar.** A path-aware expat
   scanner (whitelisted tags, `parent::child` matching) reads the CRESDA
   `<MetaInfo>` XML: identity, receive date/time, cloud percent, pixel size,
   orbit id, `SunPosGeodetic::Azimuth/Elevation`, the repeated `BandID`
   inventory, and — when the batch declares them — calibration
   (`GainVal`/`OffsetVal` flat lists or per-band
   `BandCalibration::Gain/Offset/Bias`). Nothing is fetched; nothing invented.

3. **Absence is absence, per field.** `ProductMetadata` gains
   `sensorMode`, `orbitId`, `hasSunElevation/sunElevationDeg`,
   `hasSunAzimuth/sunAzimuthDeg`, `bandCalibration[]` (per-band, gain and
   bias independently flagged), `declaredBandIds`. `toJson()` serializes the
   flags; import operators report `missingDeclaredFields[]` so consumers
   distinguish "not declared" from "declared zero". L1A pixels are stamped
   `SICNU_RADIOMETRIC_STATE=digital_number`; published per-date coefficient
   tables remain the classroom path for TOA conversion.

4. **Band roles are data-driven and fail-closed.**
   `data/products/band_roles/{gaofen,zy3,hj}.json` map declared band ids →
   ADR 0065 roles with documented centre wavelengths (range midpoints; FWHM
   intentionally omitted). The loader is Qt-free (SICNU_DATA_DIR → walk-up →
   compiled source dir), caches per family file, and a missing/unparseable
   table is a structured error. A band with no canonical role (GF-6 WFV
   violet/yellow) is `unknown` **with a required `role_reason`** — the
   loader rejects an unexplained `unknown`. Never mapped by band number.

5. **PMS/NAD pan-vs-MS resolution is inventory-driven.** PMS pan and MS
   files share `BandID` letters; the sidecar `ModeID` (PAN) or a 1-band
   inventory selects the `_pan` table. ZY-3 NAD pan/MS is resolved the same
   way. Sidecar-declared shape decides — no positional guessing.

6. **Imports reuse the existing stacking contract.**
   `rs:gaofen_import` / `rs:zy3_import` / `rs:hj_import` (one class per
   family, shared machinery in `rs_cn_import_operator.h`) build a
   `SatelliteProducts::ProductInfo` (band order = declared `BandID` order =
   TIFF band index) and stack through the unmodified `stackToGeoTiff`
   (windowed, fail-closed on unresolvable band lists, #676/#634 semantics).
   CN metadata is stamped afterwards: dataset-level
   `SICNU_PRODUCT_TYPE/FAMILY/SENSOR(_MODE)/SUN_ELEVATION_DEG/
   SUN_AZIMUTH_DEG/ORBIT_ID` + `digital_number` state (plus a plain
   `SUN_ELEVATION` mirror for the radiometric/DOS consumers); band-level
   `SICNU_CALIB_GAIN_<band>`/`SICNU_CALIB_BIAS_<band>` (radiance =
   DN × GAIN + BIAS, CRESDA GainVal/OffsetVal semantics; indexed by the
   **stacked** band order, not the sidecar inventory) beside the role and
   wavelength items ADR 0065 already defined. Registration goes through the
   explicit `initBuiltinRsOperators()` list (#707: static registrars are
   dead-stripped in static-link builds).

7. **Registry integration.** One `ProductAdapter` per family in
   `cn_product_adapters.cpp`, registered before the GenericRaster fallback;
   `detectProductKind` claims supported CN families and routes recognized-
   but-unsupported names to `Unknown` so auto-reading refuses diagnosably.

## Consequences

- GF/ZY/HJ products become product-aware end to end: headless
  import → NDVI (role-resolved) → supervised classification runs on
  synthetic fixtures in `tests/test_cn_products.cpp`.
- D8 (operator capability metadata) and D3 (lab flows) can key off the
  stable `ProductMetadata` fields and `SICNU_*` output keys; neither side
  rewrites the other.
- Documents that only *declare* what the sidecar declares: teaching
  workflows that need coefficients outside the sidecar read the published
  tables deliberately, outside this contract.
- Tag variants across CRESDA generations are absorbed by the whitelisted
  synonyms; a tag outside the whitelist is reported missing (extensible by
  widening the whitelist, not by guessing).
