/***************************************************************************
  processing/algorithms/temporal_smoothing.h
  Temporal Phenology Timeline Studio (D16) — robust time-series smoothing.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Pure, allocation-lean smoothing kernels for regular-calendar series
  (NaN = missing). Every function returns a series the same size as @a y.

  Namespace rule (ADR 0161 / DECISIONS D-160-4, revised after review): the
  kernels live in the nested namespace `sicnu::temporal::d16`.
  `whittakerSmoothRobust` and `savitzkyGolay` would otherwise collide at the
  symbol level with the same-signature functions of
  processing/algorithms/temporal/temporal_fit.h (the D10 lineage compiled
  into the shared sicnu_processing library) — ELF interposition would let one
  library's calls silently bind to the other implementation. The nested
  namespace keeps the spec's function names while making the mangling
  distinct; both headers may now be included in one translation unit.

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

/// D16 smoothing seam (see the namespace rule above).
namespace d16
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
/// (<= 0 means a single plain pass)
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
/// NaN (no fabricated bridging) — a NaN center sample itself also stays NaN,
/// matching the gap-fidelity rule above rather than the bridging variant of
/// the temporal_fit lineage.
std::vector<float> savitzkyGolay( const std::vector<float> &y,
                                  int windowSize,
                                  int polynomialDegree );

} // namespace d16
} // namespace sicnu::temporal

#endif // SICNU_PROCESSING_ALGORITHMS_TEMPORAL_SMOOTHING_H
