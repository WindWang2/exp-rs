/***************************************************************************
  processing/algorithms/temporal_smoothing.h
  Temporal Phenology Timeline Studio (D16) — robust time-series smoothing.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Pure, allocation-lean smoothing kernels for regular-calendar series
  (NaN = missing). Every function returns a series the same size as @a y.

  Include rule (ADR 0161 / DECISIONS D-160-4): this header is
  overload-adjacent to processing/algorithms/temporal/temporal_fit.h — both
  declare `sicnu::temporal::whittakerSmooth`. Never include both in one
  translation unit; D16 code and tests include only this header.

  Numeric contract:
    * NaN samples are absent, never zero (their effective weight is 0);
    * a series with no finite sample yields all-NaN (the smoother is not
      defined without data — no silent identity);
    * determinism: fixed evaluation order, single-threaded;
    * tolerance grade 1e-4 (banded solve; behavioral assertions only).
 ***************************************************************************/

#ifndef SICNU_PROCESSING_ALGORITHMS_TEMPORAL_SMOOTHING_H
#define SICNU_PROCESSING_ALGORITHMS_TEMPORAL_SMOOTHING_H

#include <vector>

namespace sicnu::temporal
{

/// Weighted Whittaker smoother: minimize Σ w_i (y_i − z_i)² + λ Σ (Δᵈ z)².
/// Pentadiagonal banded Cholesky solve for d = 2, tridiagonal Thomas solve
/// for d = 1 — both O(n) time and space. Other d returns an empty vector
/// (documented refusal, not a guess). λ > 0; λ → 0 approaches interpolation
/// of the finite samples; huge λ approaches a degree-d polynomial.
/// @a w empty means weight 1 for finite samples, 0 for NaN.
std::vector<float> whittakerSmooth( const std::vector<float> &y,
                                    const std::vector<float> &w,
                                    double lambda,
                                    int d = 2 );

/// Robust iterative Whittaker smoother (Cauchy IRLS): @a iterations rounds
/// of reweighting w_i ∝ w0_i / (1 + (r_i / (c·σ̂))²) with c = 3 and
/// σ̂ = 1.4826·MAD(residuals). Asymmetric negative spikes (undetected clouds,
/// shadows) get damped instead of smeared — the fit hugs the upper envelope.
/// Same contract as whittakerSmooth otherwise.
std::vector<float> whittakerSmoothRobust( const std::vector<float> &y,
                                          const std::vector<float> &w,
                                          double lambda,
                                          int iterations = 3 );

/// Savitzky–Golay local polynomial convolution. Window must be odd and >= 3;
/// 1 <= polynomialDegree <= 4 (outside → empty vector). Boundary points are
/// fit with the largest window available at each end (shrink-at-boundary);
/// positions whose window holds fewer finite samples than degree + 1 stay
/// NaN (no fabricated bridging).
std::vector<float> savitzkyGolay( const std::vector<float> &y,
                                  int windowSize,
                                  int polynomialDegree );

} // namespace sicnu::temporal

#endif // SICNU_PROCESSING_ALGORITHMS_TEMPORAL_SMOOTHING_H
