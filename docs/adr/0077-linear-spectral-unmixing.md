# ADR 0077: Linear Spectral Unmixing

## Context

The hyperspectral stack gained dimensionality reduction (MNF, ADR 0075) and
dual-metric matching (SAM/SID, ADR 0076) but had no mixture analysis: no way
to estimate per-pixel endmember abundances. The mission's D-series lists
linear spectral unmixing with abundance maps and reconstruction error.

## Decision

1. **Kernel** (`SpectralUnmixing`, `src/processing/algorithms/spectral_unmixing.{h,cpp}`):
   `unmix(pixels, count, bands, endmembers, nEndmembers, result, error)` —
   per pixel, solve the least-squares system `E a = x` via normal equations
   with a 1e-9 ridge on the Gram diagonal (Gaussian elimination with partial
   pivoting); clip abundances to [0,1] and renormalize to unit sum
   (approximate fully constrained unmixing); report the per-pixel RMSE
   `||x − E a||/√bands`. Requires `1 ≤ nEndmembers ≤ bands`; singular systems
   leave abundances at zero with the pixel norm as error.

2. **Operator** `rs:spectral_unmixing` (`input`, `output`, `endmembers`
   array-of-arrays, optional `errorOut`, optional `bands`): writes one
   abundance band per endmember plus an optional reconstruction-error band;
   the result JSON reports `endmembers` and the mean `meanError`. Registered
   through the normal registry; FullRaster memory policy.

## Consequences

- Hyperspectral mixture analysis is available end-to-end (kernel + operator +
  headless tests): known-mixture abundance recovery, positive error off the
  simplex, argument guards, and an operator execution test writing the
  abundance bands.
- The least-squares-with-ridge + clip + renormalize formulation is documented
  as an approximation to fully constrained unmixing (FCLS), which remains a
  possible follow-up alongside endmember extraction (D7).
