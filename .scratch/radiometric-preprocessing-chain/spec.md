# Specification: Radiometric Calibration & QUAC Atmospheric Correction

**Feature Name:** Quantitative Preprocessing Chain (Radiometric Calibration + QUAC)
**Status:** `READY_FOR_AGENT`
**Target Path:** `.scratch/radiometric-preprocessing-chain/spec.md`
**Date:** 2026-08-05

---

## Problem Statement

Remote sensing imagery from satellite sensors (Landsat, Sentinel-2, MODIS) is delivered as raw digital numbers (DN) that are not directly comparable across sensors, dates, or atmospheric conditions. Before quantitative analysis—spectral indices, change detection, classification, or multi-temporal comparison—DN values must be converted to physical units (radiance, TOA reflectance, or brightness temperature) using sensor-specific calibration coefficients, and atmospheric effects must be removed or mitigated.

Prior to this work, the system had a **dead workflow reference** `tool.rs.radiometric_calibration` pointing to an unregistered operator `rs:radiometric_calibration`, and the only DN-to-radiance capability was a manual gain/bias conversion buried inside the atmospheric correction operator. No TOA reflectance, no brightness temperature, no metadata-driven coefficient resolution, and no Sentinel-2 MTD XML parsing existed. The atmospheric correction supported only DOS1/DOS2 (Chavez 1996), with no image-statistics-based method for scenes lacking radiometric gain/bias metadata.

## Solution

A two-part quantitative preprocessing chain:

1. **`rs:radiometric_calibration` Operator** — converts DN to radiance, TOA reflectance, or brightness temperature. Coefficients are resolved from sensor metadata files (Landsat MTL.txt, Sentinel-2 MTD XML) or GDAL-embedded scale/offset, with band-description-based mapping to associate stacked-raster bands with the correct MTL/MTD coefficients.

2. **QUAC (Quick Atmospheric Correction) method** — added to the existing `rs:atmospheric_correction` operator as a fourth method alongside DN-to-Radiance, DOS1, and DOS2. QUAC is a multi-band, image-statistics-driven method (Bernstein 2008) that estimates surface reflectance in [0, 1] without requiring external gain/bias or atmospheric model parameters.

## User Stories

1. As a remote sensing analyst, I want to convert Landsat DN values to radiance using MTL coefficients, so that I can perform quantitative spectral analysis.
2. As a remote sensing analyst, I want to convert Landsat DN values to TOA reflectance using MTL REFLECTANCE_MULT/ADD and sun elevation, so that I can compare imagery across dates and sensors.
3. As a remote sensing analyst, I want to convert thermal band DN to brightness temperature using K1/K2 constants, so that I can derive land surface temperature.
4. As a remote sensing analyst, I want to convert Sentinel-2 DN to reflectance using MTD quantification values, so that I can use L2A surface reflectance products.
5. As a remote sensing analyst, I want the calibration operator to automatically read coefficients from the sensor metadata file I provide, so that I don't have to manually look up gain/bias values.
6. As a remote sensing analyst, I want the operator to fall back to GDAL-embedded scale/offset when no metadata file is available, so that MODIS and other generic products can be calibrated.
7. As a remote sensing analyst, I want to calibrate all bands of a multi-band raster in one operation, so that I don't have to run the operator per-band.
8. As a remote sensing analyst, I want to select specific bands for calibration, so that I can process only the bands I need.
9. As a remote sensing analyst, I want to apply QUAC atmospheric correction to a multi-band raster, so that I can obtain approximate surface reflectance without radiometric gain/bias metadata.
10. As a remote sensing analyst, I want QUAC to produce output values clipped to [0, 1], so that the results are physically valid reflectance.
11. As a remote sensing analyst, I want the atmospheric correction dialog to automatically hide irrelevant parameters (band, gain, bias, airmass) when QUAC is selected, so that the UI is not confusing.
12. As an AI agent user, I want `rs:radiometric_calibration` to be available as a tool-call, so that I can invoke it through the Copilot or MCP server.
13. As a workflow user, I want `tool.rs.radiometric_calibration` to resolve to a real operator, so that the workflow tool definition is not dangling.
14. As a developer, I want the operator to error explicitly when a band lacks calibration coefficients, so that silent wrong output (raw DN passed through as "radiance") is impossible.
15. As a developer, I want the TOA reflectance formula to be selected by sensor type rather than coefficient heuristics, so that a Landsat band missing REFLECTANCE_MULT/ADD doesn't silently take the generic scale/offset path.
16. As a developer, I want QUAC to guard against divide-by-zero when the scene is all-dark, so that inf/NaN doesn't propagate to output.

## Implementation Decisions

### Metadata resolution strategy

The operator accepts an optional `metadata_path` parameter. When provided, the metadata file type is detected by filename:
- `*_MTL.txt` → Landsat MTL parsing (reuses the now-public `SatelliteProducts::parseMtlKeyValues`, which strips GROUP/END lines and returns a flat key/value map)
- `MTD_MSI*.xml` → Sentinel-2 MTD XML parsing (Qt `QDomDocument`)

When `metadata_path` is empty, the operator falls back to reading GDAL-embedded band metadata (`GDALGetRasterScale`/`GDALGetRasterOffset` and `SUN_ELEVATION` dataset metadata).

### Band-to-coefficient mapping

Stacked rasters may have bands in a different order than the sensor's native band numbering. The operator reads GDAL band descriptions (e.g. "B4", "B10") to map raster band index → sensor band number. When no descriptions are set, it falls back to identity mapping (raster band i = MTL band i) using synthetic "B\<index\>" names.

### Landsat MTL coefficient fields

Reads per-band: `RADIANCE_MULT_BAND_N`, `RADIANCE_ADD_BAND_N`, `REFLECTANCE_MULT_BAND_N`, `REFLECTANCE_ADD_BAND_N`, `K1_CONSTANT_BAND_N`, `K2_CONSTANT_BAND_N`. Scene-level: `SUN_ELEVATION`. Collection 1 fallback: unsuffixed `RADIANCE_MULT`/`RADIANCE_ADD`. Coefficients are only assigned when the MTL key exists and parses as a double; missing keys preserve the `BandCoefficients` defaults.

### Sentinel-2 MTD parsing

Processing level detected from the XML root tag name (`Level-2A` → L2A, `Level-1C` → L1C). L2A reads `BOA_ADD_OFFSET` list + `BOA_QUANTIFICATION_VALUE` scalar. L1C reads `RADIO_ADD_OFFSET` + `RADIO_QUANTIFICATION_VALUE`. Sun zenith from `Mean_Sun_Zenith_Angle` (L2A) or `ZENITH_ANGLE` (L1C); sun elevation = 90 − zenith. Band ID mapping follows the S2 spectral band sequence (B1=0, B2=1, …, B12=12).

### Calibration kernels

- **Radiance**: `L = radianceGain * DN + radianceBias`
- **TOA reflectance** (formula selected by `SensorType`, not coefficient heuristics):
  - Landsat: `ρ = (reflMult * DN + reflAdd) / sin(sunElevation)` — requires non-default reflMult/reflAdd and valid sun elevation; fails otherwise
  - Sentinel-2 / Generic: `ρ = (DN + offset) / scale` — requires scale ≠ 0
- **Brightness temperature**: `T = K2 / ln(K1 / L + 1)` where `L = radianceGain * DN + radianceBias` — requires K1 > 0 and K2 > 0

### Missing-coefficient handling

`processFile` errors explicitly when a band has no entry in the resolved `CalibrationMetadata.bands` map. This prevents silent passthrough of raw DN as "radiance" when coefficients are missing.

### QUAC algorithm

Multi-band kernel: for each band, compute 1st percentile (dark, path-radiance proxy) and 99th percentile (bright). Scene-average bright and dark references are computed across all bands. Per-band gain: `gain = 0.5 * (meanBright - meanDark) / (range * meanBright)`. Output: `ρ = gain * DN + offset`, clipped to [0, 1]. Guards: requires ≥ 2 bands, non-degenerate dynamic range (`refRange > 0`), and non-zero `meanBright`.

### Operator contract

Follows the existing RSOperator six-method contract (`name`, `displayName`, `group`, `description`, `schema`, `metadata`, `run`). Registered via `REGISTER_RS_OPERATOR` macro in `rs_operators_init.cpp`, which auto-bridges to the `AtomicAlgorithmRegistry` for agent/MCP tool-call access. The `tool.rs.radiometric_calibration` workflow definition (already registered in `builtin_definitions.cpp`) becomes live automatically.

### `parseMtlKeyValues` public exposure

The previously file-local `parseMtlKeyValues` function in `satellite_products.cpp` was promoted to public API (`satellite_products.h`) so the radiometric calibration module can reuse it. No behavior change — purely a visibility change.

### Dead workflow reference cleanup

Three dead `registerAtomicTool` calls in `builtin_definitions.cpp` were removed: `tool.rs.ndvi` (→ `rs:ndvi`, never existed; NDVI is via `rs:spectral_index`), `tool.rs.pan_sharpening` and `tool.rs.pansharpen` (→ never existed; pansharpening is via `rs:image_fusion`).

## Testing Decisions

### Test philosophy

Tests verify external behavior (kernel math correctness, metadata parsing correctness, file-level I/O round-trip) rather than implementation details. All tests use the existing Catch2 + GDAL fixture pattern established by `test_atmospheric.cpp` and `test_band_math.cpp`.

### Three existing test seams (zero new seams)

1. **Kernel-level** (`[radcal]` / `[atm][quac]` tags): flat `std::vector<float>` inputs + `Catch::Matchers::WithinAbs` assertions. Tests each calibration kernel (`toRadiance`, `toToaReflectance`, `toBrightnessTemperature`, `quac`) with known inputs and expected outputs, plus null/zero-count guards and edge cases (missing coefficients, invalid sun elevation, zero scale, all-dark scene).

2. **File-level** (`[radcal][gdal]` / `[atm][quac][gdal]` tags): synthesizes a small GeoTIFF via `createOutputTiff` + `GDALRasterIO`, runs `processFile`/`processFileMultiBand`, reads back output via `GdalDatasetWrapper` and asserts values. Includes MTL file synthesis (`writeMtlFile` helper) and MTD XML synthesis for metadata-parsing tests.

3. **Operator registration** (`[operators][rs]` tag): `RSOperatorRegistry::instance().hasOperator("rs:radiometric_calibration")` smoke check in `test_rs_operators.cpp`.

### Prior art

`test_atmospheric.cpp` (DOS1/DOS2 kernel + file tests), `test_band_math.cpp` (expression engine + file tests), `test_spectral_indices.cpp` (index formulas + file tests), `test_rs_operators.cpp` (registration smoke tests).

## Out of Scope

- **6S / FLAASH physical atmospheric correction** — requires external radiative transfer model binaries; design reserved for future Python IPC integration.
- **Import chain persistence** — coefficients are not yet written to the stacked GeoTIFF by `rs:landsat_import` / `rs:sentinel2_import`. The calibration operator reads them from the metadata file directly. Future enhancement: persist `REFLECTANCE_MULT/ADD` etc. into GeoTIFF metadata domains at import time.
- **SAR / hyperspectral / LiDAR** — separate domains, not part of optical preprocessing.
- **Cartography / map composer completion** — independent gap, separate spec.
- **Band Math engine enhancement** (functions, conditionals) — independent gap, separate spec.

## Further Notes

- The QUAC implementation is a simplified variant of Bernstein et al. (2008); it uses fixed 1%/99% percentiles and a scene-average bright reference rather than the full per-band endmember estimation. This is adequate for approximate surface reflectance when no radiometric metadata is available, but is not a replacement for physical atmospheric correction (6S/FLAASH) for publishable quantitative work.
- The `toToaReflectance` kernel's formula selection was initially implemented as a coefficient heuristic (`reflMult != 1.0 || reflAdd != 0.0`) and was corrected during code review to use explicit `SensorType` dispatch, because a Landsat band whose MTL omits REFLECTANCE_MULT/ADD would silently take the generic scale/offset path and output raw DN as "reflectance".
- The `bandCount` field in the operator result was initially `bandIndices.size()`, which returned 0 when the `bands` parameter was omitted (default = all bands). Fixed to query the raster's actual band count in that case.
