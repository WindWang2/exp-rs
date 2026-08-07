# ADR 0078: RX Anomaly Detection

## Context

The hyperspectral stack had dimensionality reduction (MNF), matching
(SAM/SID), and mixture analysis (unmixing) but no unsupervised anomaly
detection — the mission's D9. The Reed-Xiaoli (RX) detector is the standard
first-pass anomaly detector for hyperspectral imagery: per-pixel Mahalanobis
distance to the global scene background.

## Decision

1. **Kernel** (`SpectralAnomaly`, `src/processing/algorithms/spectral_anomaly.{h,cpp}`):
   `rxDetector(pixels, count, bands, rxValues, error)` — sample mean and
   (biased) covariance over all pixels, invert with a 1e-9 ridge via
   Gauss-Jordan with partial pivoting, score each pixel by
   `RX(x) = (x−μ)ᵀ Σ⁻¹ (x−μ)`, clamped to ≥ 0. Singular background
   covariance is a typed error.

2. **Operator** `rs:rx_anomaly` (`input`, `output`): writes a single-band
   Float32 RX score raster; result JSON reports mean and max score.
   Registered; FullRaster memory policy.

## Consequences

- Unsupervised anomaly screening is available end-to-end (kernel + operator +
  tests): an injected outlier is flagged as the maximum score over a
  background cluster, argument guards are pinned, and the operator test
  verifies the score raster. The known RX sensitivity — the outlier slightly
  contaminating the global background estimate — is acknowledged in the
  kernel test and the operator metadata (a windowed local-RX variant remains
  a possible follow-up).
