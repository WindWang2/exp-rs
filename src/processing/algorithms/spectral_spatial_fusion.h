// spectral_spatial_fusion.h — spatial consistency fusion for spectral score
// rasters (Spectral Intelligence 12.0 mean window; Spectral Intelligence 13.0
// edge-preserving bilateral method).
//
// Per-pixel spectral detections (rs:matched_filter / rs:ace / rs:cem_detection
// scores, RS anomaly scores, similarity scores) are frequently thresholded
// directly, which leaves isolated single-pixel false alarms. A standard,
// verifiable enhancement is local spatial CONSISTENCY: replace each valid
// pixel's score with a convex combination of its spectral score and a local
// aggregate of its valid spatial neighborhood:
//
//   fused(p) = (1 − β)·s(p) + β·aggregate{ s(q) : q ∈ W(p), q valid }
//
// with W(p) the (2r+1)² window clamped to the raster and β ∈ [0, 1].
//
// Two aggregates (Config::method):
//
//   Mean (default, 12.0): the arithmetic mean of the valid window members.
//     The pixel itself counts as a member, so the aggregate is defined for
//     every valid pixel (an isolated valid pixel averages to its own score).
//
//   Bilateral (13.0): a range-weighted mean that preserves score edges —
//       aggregate = Σ_q G_s(p−q)·G_r(s(q)−s(p))·s(q) / Σ_q G_s(p−q)·G_r(s(q)−s(p))
//     with the spatial Gaussian G_s(d) = exp(−|d|²/(2σ_s²)), σ_s = r/2, and the
//     range Gaussian G_r(Δ) = exp(−Δ²/(2σ_r²)), σ_r = Config::sigmaRange. A
//     neighbor whose score differs from the center by many σ_r contributes
//     ~0, so a step edge is not smeared the way the plain mean smears it,
//     while a locally flat region is averaged exactly like the mean
//     (G_r ≡ 1 there). Invalid neighbors are skipped and the weights
//     renormalized — same leak-proof contract as the mean.
//
// NoData contract (leak-proof, both methods): a pixel whose score is
// non-finite (or masked invalid) is INVALID. Invalid pixels are never fused
// (fused = NaN) and never contribute to any neighbor's aggregate or count —
// spatial smoothing cannot bleed NoData scores into results, and valid pixels
// near NoData renormalize to the valid neighbors only (no zero-fill bias).
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
    /// Neighborhood aggregate method.
    enum class Method
    {
        Mean,     ///< arithmetic mean of valid window members (12.0 default)
        Bilateral ///< range-weighted mean preserving score edges (13.0)
    };

    struct Config
    {
        int radius = 1;             ///< window half-side, >= 0; 0 → fused == scores
        double beta = 0.5;          ///< spatial weight in [0, 1]; 0 → fused == scores
        Method method = Method::Mean; ///< neighborhood aggregate
        double sigmaRange = 1.0;    ///< range Gaussian sigma (score units), > 0,
                                    ///< bilateral method only
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
     *         non-positive extents, negative radius, beta outside [0, 1] or
     *         non-finite, non-positive sigmaRange on the bilateral method).
     */
    bool fuseScores( const float *scores, const uint8_t *valid, int width, int height,
                     const Config &config, Result *result,
                     QString *errorMessage = nullptr );
} // namespace SpectralSpatialFusion
