/***************************************************************************
  processing/algorithms/spatiotemporal_filter.h
  Temporal Phenology Timeline Studio (D16) — STARFM-class spatiotemporal
  fusion (simplified single-pair kernel).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Predicts the fine-resolution image at target time t_k from a base-time
  fine/coarse pair and the target-time coarse image. For every center pixel
  a (2w+1)² moving window contributes candidate predictions

      F_ij(t_k) ≈ C_ij(t_k) + F_ij(t_0) − C_ij(t_0),

  weighted by the inverse of the composite distance

      C_ij = S_ij · T_ij · D_ij,
      S_ij = |F_ij(t0) − C_ij(t0)| + εs        (spectral difference)
      T_ij = |C_ij(t_k) − C_ij(t0)| + εt       (temporal difference)
      D_ij = sqrt(dx² + dy²) + 1               (geometric distance, pixels)

  normalized to Σ W = 1 inside the window. Candidates whose fine/base value
  differs from the center pixel by more than @a spectralThreshold (heterogeneous
  pixels) are excluded; when every candidate is excluded the center pixel's
  base value carries the coarse temporal change:

      fine(t_k) = fine_center(t0) + mean_window( C(t_k) − C(t0) ),

  and when even that is undefined (NaN) the output is NaN. NaN inputs never
  become candidates; outputs are clamped to the physical reflectance range
  [0, 1]. Deterministic: fixed scan order, single-threaded.
 ***************************************************************************/

#ifndef SICNU_PROCESSING_ALGORITHMS_SPATIOTEMPORAL_FILTER_H
#define SICNU_PROCESSING_ALGORITHMS_SPATIOTEMPORAL_FILTER_H

#include <vector>

namespace sicnu::temporal
{

struct StarfmOptions
{
    int windowRadius = 15;           ///< moving search window half-width (pixels)
    int numClasses = 5;              ///< reserved class count (documentation parity)
    float spectralThreshold = 0.05f; ///< homogeneous-candidate gate on |F − F_center|
    float spatialWeightDecay = 1.0f; ///< geometric distance scale factor
};

class SpatiotemporalFilter
{
  public:
    /// Predicts the fine-resolution image at the target time. All three
    /// inputs are width×height row-major float planes (NaN = missing);
    /// the result is a same-size plane in [0, 1] (NaN where undefined).
    static std::vector<float> predictStarfm( const float *fine0, const float *coarse0,
                                             const float *coarseK, int width, int height,
                                             const StarfmOptions &options );
};

} // namespace sicnu::temporal

#endif // SICNU_PROCESSING_ALGORITHMS_SPATIOTEMPORAL_FILTER_H
