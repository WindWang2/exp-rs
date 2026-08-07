# ADR 0071: Dedicated Orthorectification Dialog

## Context

`gdal:orthorectification` (RPC/GCP via GDALWarp, optional DEM, target CRS,
resampling, output resolution) was complete and tested but unreachable from a
dedicated UI: the app had a full GCP georeferencing suite (I2I/I2M windows,
`RsGeoreferencingSession`) but no single-purpose orthorectification dialog,
and the mission's Priority 0 calls for a "dedicated professional
orthorectification UI around existing backend capability" — displaying source
CRS, target CRS, DEM, RPC/GCP model, resampling, output resolution, and error
information — rather than reimplementing orthorectification.

## Decision

1. **`OrthorectificationDialog`** (`src/app/dialogs/orthorectification_dialog.{h,cpp}`)
   over the existing `gdal:orthorectification` operator (no new algorithm):
   input raster, target CRS (default `EPSG:4326`), optional DEM file,
   resampling (bilinear default / nearest / cubic / cubicspline / lanczos),
   output resolution (0 = auto), constant-elevation fallback, optional output
   NoData. A model-status label reports whether the input carries RPC metadata
   or GCPs (and warns when neither, matching the operator's rejection).

2. **Presentation adapter discipline**: `buildParams()` is a public, headless-
   testable seam assembling the operator's parameter JSON; `onRun` submits it
   through the shared `runOperatorTask` path. The dialog never bypasses the
   operator.

3. **Menu entry**: 预处理 → "正射纠正 (RPC/GCP)...".

## Consequences

- RPC/GCP orthorectification with DEM terrain correction is reachable from the
  desktop UI with a professional parameter surface, backed entirely by the
  existing tested operator.
- The dialog's param contract is pinned by a headless Catch2 test (defaults
  omitted, explicit options surfaced), guarding against operator-schema drift.
- No algorithm duplication: orthorectification remains GDALWarp-based with the
  georeferencing suite as the GCP-interactive path.
