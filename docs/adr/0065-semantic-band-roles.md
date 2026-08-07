# ADR 0065: Semantic Band Roles for Product-Aware Workflows

## Context

Band meaning was hard-coded as positional 1-based band numbers everywhere:

- `rs:spectral_index` / `SpectralIndexDialog` defaulted NIR=4, Red=3, Green=2,
  Blue=1, SWIR=5;
- import defaults (`B1..B7`, `B2/B3/B4/B8`) and fusion "panchromatic" assumed
  sensor layouts positionally;
- the only band-semantics concept was the vendored QGIS
  `Qgis::RasterColorInterpretation`, which lacks product granularity (no
  QA/SCL/Cirrus, no OLI Coastal-vs-Blue split) and no resolution semantics.

Product discovery already read per-sensor band names and approximate centre
wavelengths (`landsatWavelength`/`sentinelWavelength`), and `stackToGeoTiff`
wrote `WAVELENGTH`/`WAVELENGTH_UNITS` band metadata that nothing consumed. The
data-collections import spec explicitly deferred "Normalized spectral metadata"
(`2026-07-25-data-collections-import-preview-spec.md`, Out-of-Scope #7).

## Decision

1. **`BandRole` domain enum** in `src/data/band_role.h` (namespace
   `sicnu::data`): `Unknown, Coastal, Blue, Green, Red, RedEdge, NIR,
   NarrowNIR, SWIR1, SWIR2, Cirrus, Panchromatic, Thermal, QA,
   SceneClassification`, with stable lowercase ids
   (`bandRoleToString`/`bandRoleFromString`, case-insensitive round-trip) and
   short UI display names (`bandRoleDisplayName`).

2. **Role + FWHM assignment at discovery**: `SatelliteProducts` assigns each
   discovered `BandFile` a role and an approximate FWHM — Landsat OLI (8/9) vs
   legacy TM/ETM (4-7) keyed on the MTL `SPACECRAFT_ID` (B1: Coastal vs Blue;
   B6/B7: SWIR vs Thermal), Sentinel-2 MSI (`B8A`→`NarrowNIR`, `SCL`→
   `SceneClassification`, `MSK_*`/`AOT`/`WVP`/`TCI`→`QA`), MODIS
   `sur_refl_b01..b07`.

3. **Carried through the catalog**: `stackToGeoTiff` writes `SICNU_BAND_ROLE`
   and `FWHM` per-band GDAL metadata (beside the existing `WAVELENGTH`);
   `ChildBandInfo` (import preview) and `RasterBandStructure` (asset structure,
   read from the band metadata by `GdalRasterSourceProvider`) both carry the
   role. `GdalDatasetWrapper::bandMetadataItem()` is the read seam.

4. **Role-first consumers, positional fallback**: `rs:spectral_index` resolves
   an omitted band parameter from the input's roles (SWIR1 then SWIR2 for SWIR)
   and keeps the positional default when no role exists; explicit band
   parameters still win. `SpectralIndexDialog` preselects band combos by role
   (labelled "波段 N (Role)") and falls back to the legacy positional mapping
   for plain rasters.

## Consequences

- Stacked product outputs (`rs:*_import`, Landsat dialog) become
  product-aware: NDVI/NDBI/etc. resolve semantically without manual band
  numbers, headless (operator) and in the dialog.
- Plain rasters keep byte-identical behavior (positional defaults).
- No operator-schema break: band parameters remain valid, explicit values
  override roles.
- The import spec's deferred "normalized spectral metadata" is now grounded:
  later work (QA masking keyed on the `QA`/`SceneClassification` roles, SCL
  discovery, calibration state, spectral library matching) can key off roles
  instead of re-deriving sensor layouts.
- `test_satellite_products.cpp` was found to be an orphan (registered in no
  target) and is now a registered CTest target so this coverage runs.
