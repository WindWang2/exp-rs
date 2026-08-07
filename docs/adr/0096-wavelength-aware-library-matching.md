# ADR 0096: Wavelength-Aware Spectral Library Matching

## Context

The spectral workbench (ADR 0092) matched a profile spectrum against library
entries with the SAM/SID kernels — but entries whose band count differed were
silently skipped, so a 10-band Sentinel-2 profile could not match a 4-band
library entry even when both carried wavelength metadata. The wavelength
resampling kernel (ADR-0079 / D4) was unused by the matching path. This made
cross-sensor library matching impossible in practice.

## Decision

`SpectralLibrary::matchSpectrum` gains a wavelength-aware overload
`matchSpectrum(spectrum, spectrumWavelengths, library, nodata)`:

- When an entry's band count equals the spectrum's, scoring is unchanged.
- When band counts differ **and both sides carry strictly increasing
  wavelength metadata**, the test spectrum is linearly resampled onto the
  entry's wavelength grid (`SpectralResampling::resampleSpectrum`) and scored
  against the entry's original spectrum; the match is flagged
  `MatchScore::resampled`.
- Entries that still cannot be compared (no wavelengths on either side, or a
  resample falling outside the source range → NaN) are skipped exactly as
  before.
- The original 3-argument overload is preserved (delegates with empty
  wavelengths), so existing callers and the SAM/SID operator are unaffected.

The `SpectralLibraryDialog` now passes the profile's wavelength grid (from the
Spectral Profile dock) and reports that resampled entries were auto-matched.

## Consequences

- Cross-sensor library matching works: an S2 profile with wavelengths can be
  compared to a library on a different grid without manual resampling — the
  workbench closes the "Spectral Profile ↔ Spectral Library → Matching" loop
  for mixed-resolution data.
- The resampling is confined to the matching seam (pure backend), covered by
  kernel-level tests (exact interpolation → angle ≈ 0, marked resampled;
  missing wavelengths → still skipped), and the dialog suite stays green.
