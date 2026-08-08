# ADR 0100: Continuum-Removal View and FWHM in the Spectral Profile

## Context

The spectral workbench (ADR 0092/0096/0097) closed the Profile ↔ Library
matching loop, but the profile chart itself was raw values only. The DoD D10
list includes "continuum removal where appropriate" as a display/analysis
aid, and D1 asks for a FWHM surface. The continuum-removal kernel
(`SpectralClassification::continuumRemoval`) and the FWHM band metadata
(ADR-0065) both existed unused at the widget boundary.

## Decision

- `SpectralProfileWidget` gains `setContinuumRemovalEnabled()` and
  `displayValues()`: when enabled, the chart draws the continuum-removed
  spectrum (reflectance / convex-hull continuum, in (0,1], computed by the
  shared kernel; NaN bands pass through) on a fixed [0,1] y-scale, while
  `values()` keeps the raw spectrum untouched. The toggle is a pure display
  transform — no data mutation, so point profiles and ROI means both work.
- The widget now reads per-band `FWHM` metadata alongside `WAVELENGTH`
  (extractProfile path) and exposes it via `fwhm()`; ROI spectra carry zero
  FWHM (no source metadata).
- The profile-widget test target gains the `sicnu_processing` link (the
  widget now depends on the spectral kernels).

## Consequences

- Absorption features become comparable across spectra of different
  brightness directly in the profile chart (raw values remain available),
  and FWHM is available for wavelength-aware library matching and future
  resampling UIs.
- Tests pin both seams: the CR transform keeps the absorption dip and stays
  in (0,1] while `values()` is unchanged (toggle on/off round trip), and
  `fwhm()` reads the stamped metadata and clears with the widget.
