// spectral_detection.h — target-constrained spectral detection kernels
// (matched filter and ACE) over streamed background statistics
// (Foundation 5.0, Milestone C).
//
// Both detectors reuse SpectralAnomaly::BackgroundStats (streaming mean and
// covariance of the scene background, with the same valid-pixel predicate —
// non-finite or declared-NoData pixels excluded) and its ridge-inverted
// covariance. Conventions (Manolakis et al. 2014, "Hyperspectral unmixing
// and detection: an ill-posed problem" / TCIMF form):
//
//   Matched filter (signed):
//     MF(x) = (t − μ)ᵀ Σ⁻¹ (x − μ)
//   Scores are signed projections onto the whitened target direction —
//   positive means "along the target", negative "opposite". Thresholding is
//   the caller's job (rs:threshold_raster downstream).
//
//   Adaptive Coherence/Cosine Estimator (squared, in [0, 1]):
//     ACE(x) = (t̃ᵀΣ⁻¹x̃)² / ((t̃ᵀΣ⁻¹t̃)(x̃ᵀΣ⁻¹x̃)),  t̃ = t−μ, x̃ = x−μ
//   1 = whitened spectra parallel to the target. Pixels whose whitened norm
//   is degenerate (x̃ᵀΣ⁻¹x̃ <= 0, e.g. exactly-background constant pixels
//   under a singular direction) score NaN.
//
// All scoring is per-pixel against precomputed models — no per-pixel heap
// allocation (scratch buffer passed in by the streaming operator).
#pragma once

#include <cstddef>
#include <vector>

namespace SpectralDetection
{

/// Precomputed target projection. Build once per scene after the background
/// mean/covariance are finalized and inverted (SpectralAnomaly::invertCovariance).
struct TargetModel
{
    std::vector<double> invCovT; ///< Σ⁻¹(t − μ)
    double tWhitenedNorm2 = 0.0; ///< t̃ᵀΣ⁻¹t̃ (ACE denominator factor)
};

/// Builds the target model. @a target has @a bands values; @a mean is the
/// finalized background mean; @a invCov the inverted background covariance
/// (bands×bands row-major). Returns false on size mismatch or a non-finite
/// model (degenerate target/background).
bool buildTargetModel( const float *target, int bands,
                       const std::vector<double> &mean,
                       const std::vector<double> &invCov,
                       TargetModel *out );

/// Matched filter score (t−μ)ᵀΣ⁻¹(x−μ); NaN when @a x has a non-finite band.
/// @a scratch must have capacity >= @a bands; reused, not resized.
float matchedFilterScore( const float *x, const TargetModel &model,
                          const std::vector<double> &mean, int bands,
                          std::vector<double> *scratch );

/// ACE score in [0,1] (see header note); NaN for degenerate whitened pixels.
float aceScore( const float *x, const TargetModel &model,
                const std::vector<double> &mean,
                const std::vector<double> &invCov, int bands,
                std::vector<double> *scratch );

} // namespace SpectralDetection
