// src/processing/algorithms/change_detector.h — D15 Package E public seam.
//
// Pixel-level change detection operators on paired float rasters:
// difference / normalized difference / log-ratio, multi-band Change Vector
// Analysis (magnitude + [0,2pi) direction + adaptive threshold mask) and a
// PCA (minor-component) difference score.  Multi-band pointers use
// band-sequential layout (plane b at offset b*width*height).
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace rs::processing
{

/// Operator selector kept for callers that dispatch symbolically (UI/agent
/// tables); the numeric seams below are the primary API.
enum class ChangeDetectionType
{
    Difference,
    Ratio,
    ChangeVectorAnalysis,
    PcaDifference,
};

struct CvaChangeResult
{
    std::vector<float> changeMagnitude;      ///< ||x2 - x1||_2 per pixel
    std::vector<float> changeDirectionAngle; ///< atan2(dband1, dband0) in [0, 2pi)
    std::vector<uint8_t> binaryChangeMask;   ///< 1 when magnitude >= threshold
    float computedThreshold{ 0.0f };         ///< mean + multiplier * stddev
};

class ChangeDetector
{
  public:
    /// t2 - t1 per element (NaN propagates).  Size mismatch -> {}.
    static std::vector<float> computeDifference( std::span<const float> t1,
                                                 std::span<const float> t2 );

    /// (t2 - t1) / (t2 + t1 + epsilonGuard).  Size mismatch -> {}.
    static std::vector<float> computeNormalizedDifference( std::span<const float> t1,
                                                           std::span<const float> t2 );

    /// ln((t2 + eps) / (t1 + eps)); identical inputs yield exactly 0.
    static std::vector<float> computeLogRatio( std::span<const float> t1,
                                               std::span<const float> t2,
                                               float epsilon = 1e-4f );

    /// Multi-band CVA.  Statistics for the adaptive threshold use finite
    /// magnitudes only; non-finite pixels get magnitude NaN, direction NaN
    /// and mask 0.  Direction is the trajectory in the first two bands'
    /// plane (exact branch values pi/2, pi, 3pi/2 on the axes; 0 for a
    /// zero vector); with bands == 1 the direction is 0 everywhere.
    static CvaChangeResult computeCva( const float *t1MultiBand,
                                       const float *t2MultiBand,
                                       int width,
                                       int height,
                                       int bands,
                                       float thresholdStdDevMultiplier = 1.5f );

    /// |projection of the per-pixel difference onto the minor principal
    /// component of the difference distribution| — the non-stationary change
    /// score.  Degenerate (all-equal) differences produce zeros.
    static std::vector<float> computePcaDifference( const float *t1MultiBand,
                                                    const float *t2MultiBand,
                                                    int width,
                                                    int height,
                                                    int bands );
};

} // namespace rs::processing
