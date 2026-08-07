# ADR 0069: Radiometric Calibration Workflow Integration

## Context

`rs:radiometric_calibration` (DN → radiance / TOA reflectance / brightness
temperature, streaming via `GdalBlockStream`) was registered and tested but
unreachable from the desktop UI — no menu entry — and required the caller to
supply `metadata_path` explicitly. The mission's Priority 0 expects the
normal experience to be "Input product → metadata automatically detected →
calibration parameters automatically resolved → user selects physical output →
processing runs", with manual gain/bias entry only as a fallback.

## Decision

1. **Metadata auto-detection** (`RadiometricCalibration::autoDetectMetadataFile`):
   scans the input raster's directory for a sibling Landsat `*_MTL.txt` or
   Sentinel-2 `MTD_MSI*.xml`; when both families are present, prefers the one
   matching the raster's embedded `SICNU_PRODUCT_TYPE` metadata (Landsat MTL
   on ambiguity). `rs:radiometric_calibration` calls it when `metadata_path`
   is omitted, so CLI/MCP/UI all get automatic resolution; the embedded GDAL
   scale/offset fallback remains the last resort.

2. **`RadiometricCalibrationDialog`** (预处理 → "辐射定标..."): input raster,
   output unit (radiance / TOA reflectance / brightness temperature), all-bands
   vs single-band selection, metadata path (explicit browse or auto-detected),
   and a live status preview of the resolved coefficients (band count,
   spacecraft, processing level, sun elevation) via `loadMetadata`. Runs the
   operator through the shared `runOperatorTask` seam.

## Consequences

- The calibration step of the analysis-ready pipeline is reachable from the
  desktop UI with automatic metadata resolution — no manual coefficient entry
  for products whose metadata file sits beside the imagery.
- The auto-detection rule is shared by the operator (headless) and the dialog
  (UI), keeping the "UI is a presentation adapter" norm.
- Generic rasters without sibling metadata keep the GDAL scale/offset path.
