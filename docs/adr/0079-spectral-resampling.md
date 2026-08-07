# ADR 0079: Spectral Resampling

## Context

The spectral foundations lacked wavelength-aware resampling: comparing an
imaging spectrometer's bands to a spectral library, or to Landsat / Sentinel-2
band positions, required manual band matching. The A3 stacking work already
writes per-band `WAVELENGTH` metadata (ADR 0065), so a wavelength-aware
resampling operator can read the source grid directly.

## Decision

1. **Kernel** (`SpectralResampling`, `src/processing/algorithms/spectral_resampling.{h,cpp}`):
   `resampleSpectrum(src, srcWl, srcBands, dstWl, dstBands, out)` — linear
   interpolation between strictly-increasing source band centers; target
   wavelengths outside the source range yield NaN; invalid arguments
   (null pointers, < 2 source bands, empty targets, non-increasing source
   wavelengths) return false.

2. **Operator** `rs:spectral_resample` (`input`, `output`, `wavelengths`
   target nm array, optional `sourceWavelengths`): resolves source
   wavelengths from each band's `WAVELENGTH` metadata (product-stacked) or the
   explicit array, resamples every pixel, writes a Float32 raster with one
   band per target wavelength. Registered; FullRaster policy.

## Consequences

- Cross-sensor spectral comparison becomes a one-step, wavelength-aware
  workflow: an imaging spectrometer can be resampled onto Landsat/Sentinel-2
  band positions (or any target grid) with interpolation and honest NaN for
  out-of-range targets.
- The kernel's interpolation, guards, and out-of-range semantics are pinned by
  tests; the operator test covers both the WAVELENGTH-metadata path (A3
  integration) and the explicit-wavelengths path.
