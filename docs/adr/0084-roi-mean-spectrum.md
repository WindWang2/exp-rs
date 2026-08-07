# ADR 0084: ROI Mean Spectrum Extraction

## Context

The spectral-analysis workflow (mission D10) starts from "Raster / ROI →
Spectral Profile"; the profile dock covered single-pixel spectra
(wavelength-aware since ADR 0082), but there was no ROI/polyline mean
spectrum — the input side for library matching over an area instead of a
single pixel.

## Decision

**`SpectralRoiProfile::meanSpectrum`** (`src/processing/algorithms/spectral_roi.{h,cpp}`):
given a raster path and a polygon ROI in map coordinates, compute the
per-band mean and stddev over the pixels whose centers fall inside the
polygon (map→pixel via the geotransform; only the polygon's pixel-space
bounding box is scanned). The result also carries the band `WAVELENGTH` grid
(aligned with the profile / resampling surfaces) and the pixel count. An
out-of-raster ROI returns zero pixels with NaN means instead of erroring; a
degenerate polygon or unreadable raster is a typed error.

## Consequences

- The spectral workbench's input side exists as a pure, headless-testable
  domain function: mean/stddev over an ROI (verified against hand-computed
  values), wavelength alignment, and guard cases.
- A future workbench UI (ROI rubber-band on the canvas → profile over the
  ROI → library match) can build on this without rework.
