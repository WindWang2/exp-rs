# ADR 0097: ROI Mean-Spectrum Tool (Spectral Workbench Input Side)

## Context

The DoD spectral workflow starts from "Raster / ROI → Spectral Profile →
Spectral Library → Matching". The ROI mean-spectrum kernel existed
(`SpectralRoiProfile::meanSpectrum`, ADR-0084) and the workbench matching
dialog existed (ADR 0092/0096), but the ROI input side had no UI: only a
single-pixel profile (via the Identify tool) could feed the workbench, and the
ROI kernel was dead code at the app boundary.

## Decision

- `SpectralProfileWidget` gains `setSpectrum(values, wavelengths, labels,
  layerName)` — displays a precomputed spectrum (e.g. an ROI mean) in the same
  chart as a point profile, with the same wavelength-aware x-axis; mismatched
  auxiliary vectors are dropped and an empty spectrum clears the widget.
- A new map tool `RsRoiSpectrumTool` (rubber-band polygon) computes the ROI
  mean spectrum over the active raster on polygon release
  (`SpectralRoiProfile::meanSpectrum`, with canvas→raster CRS transform when
  needed) and reports values/wavelengths/labels through a callback, then
  schedules its own deletion.
- Wiring: "光谱分析 → ROI 均值谱..." activates the tool on the canvas; on
  completion the result is shown in the Spectral Profile dock (so the user can
  immediately run 光谱库匹配 on the ROI spectrum) and the canvas returns to
  the Identify tool.

## Consequences

- The spectral workflow's input side is complete: pixel profile *or* ROI mean
  spectrum → profile dock → library matching (with wavelength-aware
  resampling, ADR 0096) → ranked SAM/SID scores → save to library.
- The widget seam is tested headlessly (values/wavelengths/labels round trip,
  mismatched-auxiliary fallback, clear-on-empty); the ROI kernel already has
  coverage (ADR-0084), so the new code reuses tested building blocks.
