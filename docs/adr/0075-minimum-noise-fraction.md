# ADR 0075: Minimum Noise Fraction (MNF) Transform

## Context

The mission's hyperspectral priorities require meaningful dimensionality
reduction; only PCA existed (`rs:pca`). MNF — the standard noise-whitened
dimensionality reduction for hyperspectral imagery — was absent, and the
mission lists it first among the hyperspectral workflows.

## Decision

1. **Kernel** (`ImageEnhancement::mnf`): MNF as PCA of noise-whitened data —
   noise covariance estimated from lagged (shift) differences, whitened via
   the noise eigenvectors (`W = E D^{-1/2}`, eigenvalue floor 1e-9), then an
   eigen-decomposition of the whitened data's covariance orders components by
   signal-to-noise ratio. Reuses the existing `computeCovarianceMatrix` /
   `jacobiEigen` helpers; `MnfResult` carries the components plus per-component
   `signalToNoise`. `processMnfFile` streams band reads and writes the
   component GeoTIFF (same grid as input).

2. **Operator** `rs:mnf` (`input`, `output`, `numComponents`; 0 = all bands),
   registered and surfaced through the normal registry; memory policy is
   FullRaster (like PCA — honest per ADR 0073).

## Consequences

- Hyperspectral users get a standard, testable dimensionality-reduction path:
  the kernel test pins SNR ordering (first component correlates with the
  smooth-signal band over a noise band) and the operator test writes the
  requested component count.
- The whitening formulation (rather than a two-covariance generalized
  eigen-solve) stays numerically simple and reuses the existing eigen path.
- Follow-ups from the mission's hyperspectral list remain: SAM/SID matching
  depth, endmember extraction, spectral unmixing, RX anomaly detection.
