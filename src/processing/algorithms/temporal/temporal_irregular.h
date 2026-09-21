// src/processing/algorithms/temporal/temporal_irregular.h
// Kernels for irregularly sampled time axes (Temporal Phenology 12.0, WP2).
//
// The position-based smoothers in temporal_fit.h (savitzkyGolay, whittaker*,
// movingAverage) treat sample index as time, which is only correct on a
// uniform grid. The kernels here take the real time axis (days since a fixed
// epoch, same convention as tDays in temporal_collection.h) so window widths
// and roughness penalties are measured in days, not positions.
//
// Failure contract: every kernel returns an all-NaN vector (same length as the
// input) on invalid input instead of throwing or fabricating values:
//   - size mismatch between y / tDays / weights;
//   - non-finite tDays;
//   - non-increasing tDays (duplicate instants make the time metric
//     degenerate — aggregate keep_all duplicates before smoothing);
//   - non-positive window / lambda.
#pragma once

#include <cstdint>
#include <vector>

namespace sicnu::temporal
{

/// Per-sample provenance for a gap-filled series (WP2 oracle: distinguish
/// observed / interpolated / unavailable samples).
enum class SampleProvenance : std::uint8_t
{
  Unavailable = 0,  ///< NaN in input and output — unfillable position
  Observed = 1,     ///< finite input — value carried through verbatim
  Interpolated = 2, ///< NaN input filled by gap-fill
};

/// Derives per-sample provenance by comparing the gap-fill input @a input with
/// its output @a output (both length n; mismatch → empty vector). Pure function
/// of the pair — works with any fill method without touching the kernel.
std::vector<std::uint8_t> gapFillProvenance( const std::vector<float> &input,
                                             const std::vector<float> &output );

/// Day-window moving average: for each position i, the mean of the finite
/// y[j] with |tDays[j] − tDays[i]| ≤ @a windowDays/2. Positions whose window
/// contains no finite sample get NaN (documented — a gap wider than the
/// window is not fabricatable). @a windowDays must be > 0.
std::vector<float> movingAverageDays( const std::vector<float> &y,
                                      const std::vector<double> &tDays,
                                      double windowDays );

/// Time-aware Savitzky–Golay: local polynomial of degree
/// @a polynomialDegree (1–4) fitted in *days* over samples inside the
/// centered ±windowDays/2 window and evaluated at tDays[i]. Unlike the
/// position-based variant this removes the lag bias that an asymmetric
/// irregular window would introduce on sloped limbs. @a windowDays must be
/// > 0; windows with fewer than (degree + 1) finite samples return NaN.
std::vector<float> savitzkyGolayDays( const std::vector<float> &y,
                                      const std::vector<double> &tDays,
                                      double windowDays, int polynomialDegree );

/// Whittaker smoother on an irregular time axis. Solves
///   (W + λ·Dᵀ C D) z = W y
/// where D is the second divided-difference operator
///   (Dz)_r = 2·( Δz_{r+1}/h_{r+1} − Δz_r/h_r ) / (h_r + h_{r+1})
/// and C = diag((h_r + h_{r+1})/2) weights each term by the local cell width,
/// i.e. a discretized ∫(z″)²dt. On a unit-spaced grid this reduces exactly to
/// whittakerSmooth's Σ(Δ²z)² penalty.
///
/// @a w is an optional per-sample reliability weight (same contract as
/// whittakerSmooth). NaN samples get weight 0 and receive the smoothed
/// interpolation — Whittaker semantics fill gaps smoothly; callers needing
/// provenance should combine with gapFillProvenance().
std::vector<float> whittakerSmoothTime( const std::vector<float> &y,
                                        const std::vector<double> &tDays,
                                        const std::vector<float> &w,
                                        double lambda );

/// Robust (IRLS) variant of whittakerSmoothTime: iteratively re-weights
/// finite samples by a Cauchy kernel on the residual scale (3·1.4826·median
/// |r|, same convention as whittakerSmoothRobust). @a iterations is clamped
/// to [1, 10].
std::vector<float> whittakerSmoothTimeRobust( const std::vector<float> &y,
                                              const std::vector<double> &tDays,
                                              const std::vector<float> &w,
                                              double lambda, int iterations );

} // namespace sicnu::temporal
