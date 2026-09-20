// spectral_spatial_fusion.h — spatial consistency fusion for spectral score
// rasters (Spectral Intelligence 12.0, work package B).
//
// Per-pixel spectral detections (rs:matched_filter / rs:ace / rs:cem_detection
// scores, RS anomaly scores, similarity scores) are frequently thresholded
// directly, which leaves isolated single-pixel false alarms. A standard,
// verifiable enhancement is local spatial CONSISTENCY: replace each valid
// pixel's score with a convex combination of its spectral score and the mean
// score of its valid spatial neighborhood:
//
//   fused(p) = (1 − β)·s(p) + β·mean{ s(q) : q ∈ W(p), q valid }
//
// with W(p) the (2r+1)² window clamped to the raster and β ∈ [0, 1]. The
// pixel itself counts as a member of W(p), so the spatial term is defined for
// every valid pixel (an isolated valid pixel averages to its own score).
//
// NoData contract (leak-proof): a pixel whose score is non-finite is INVALID.
// Invalid pixels are never fused (fused = NaN) and never contribute to any
// neighbor's mean or count — spatial smoothing cannot bleed NoData scores
// into results, and valid pixels near NoData renormalize to the valid
// neighbors only (no zero-fill bias).
//
// The transform is a pure function of (window, plane) with no cross-tile
// state: a tile computed with an r-pixel halo has interior pixels identical
// to the whole-plane result (tile-agnostic determinism), matching the
// SpectralLocalRx streaming convention.
#pragma once

#include <QString>

#include <cstdint>
#include <vector>

namespace SpectralSpatialFusion
{
    struct Config
    {
        int radius = 1;    ///< window half-side, >= 0; 0 → fused == scores
        double beta = 0.5; ///< spatial weight in [0, 1]; 0 → fused == scores
    };

    struct Result
    {
        std::vector<float> fused;           ///< per-pixel fused score; NaN = invalid
        std::vector<uint8_t> covered;       ///< 1 when the pixel was valid and fused
        std::vector<int32_t> neighborCount; ///< valid window members per pixel (0 for invalid)
    };

    /**
     * Fuses a single-band score plane.
     *
     * @param scores row-major plane (width*height floats)
     * @param valid  optional validity mask (1 = valid). When null, validity is
     *        derived from the scores themselves (finite ⇒ valid).
     * @return false only for structurally invalid arguments (null scores,
     *         non-positive extents/bands-free plane, negative radius,
     *         beta outside [0, 1] or non-finite).
     */
    bool fuseScores( const float *scores, const uint8_t *valid, int width, int height,
                     const Config &config, Result *result,
                     QString *errorMessage = nullptr );
} // namespace SpectralSpatialFusion
