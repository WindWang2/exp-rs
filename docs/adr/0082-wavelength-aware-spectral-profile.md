# ADR 0082: Wavelength-Aware Spectral Profile

## Context

The spectral profile dock (`SpectralProfileWidget`) plotted pixel values
against a band-index x-axis; the A3 product stacking already writes per-band
`WAVELENGTH` metadata (ADR 0065), so a wavelength-aware x-axis was the
natural surface for that data — and the mission's D3 asks for wavelength-aware
spectral profiles.

## Decision

`SpectralProfileWidget` reads each band's `WAVELENGTH` metadata (nm) during
extraction and:
- exposes it via a new `wavelengths()` accessor (0.0 for bands without a
  wavelength; empty grid when the raster has none);
- scales the chart x-axis by wavelength when every band carries a valid
  wavelength (shared `xFractionForBand` used by both the line and the axes),
  with the axis title switching to "波长 Wavelength (nm)";
- falls back to the band-index axis otherwise.

## Consequences

- Product-stacked spectra (e.g. imported Sentinel-2/Landsat, ADR 0065) plot on
  a physical wavelength axis — spacing between points now reflects spectral
  distance, not band order.
- The widget keeps the accessor contract testable: the suite covers the
  WAVELENGTH-populated, plain-raster, and clear() cases (56 assertions).
- The domain stays widget-free: wavelength data still originates in the
  product model; this is a presentation surface only.
