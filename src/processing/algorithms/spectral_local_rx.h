// src/processing/algorithms/spectral_local_rx.h — local / dual-window RX
// anomaly detection (Spectral Intelligence 11.0, work package A).
#pragma once

#include <QString>

#include <cstdint>
#include <vector>

/// Dual-window Reed–Xiaoli local anomaly detection.
///
/// The global RX (SpectralAnomaly::rxDetector) scores every pixel against
/// scene-wide background statistics. This kernel scores each pixel against
/// statistics of its spatial NEIGHBORHOOD: an outer window collects the
/// local background, an inner guard window (centered on the pixel) is
/// excluded from it so a compact anomaly cannot contaminate its own
/// background estimate.
///
///   RX(x) = (x - mu_w)^T Sigma_w^-1 (x - mu_w)
///
/// with mu_w / Sigma_w the sample mean and (N-1) covariance of the valid
/// background pixels inside (outer window minus guard window).
///
/// Numerical strategy: the covariance receives a scaled diagonal loading
///     Sigma_w + alpha * (tr(Sigma_w)/B) * I        (DECISIONS D2)
/// — proportional to the data variance, unlike the global kernel's absolute
/// ridge — so small-sample windows stay invertible with a predictable bias.
/// A window whose valid-background count is below minBackgroundSamples does
/// not produce a fake score: the pixel is left unscored (NaN) and the
/// quality plane records the shortfall.
///
/// Edge handling: windows are clamped to the raster bounds. Border pixels
/// therefore use a smaller, unbiased background rather than replicated
/// copies of themselves (replication would skew the statistics toward the
/// border value). The streaming operator must still supply a full
/// outer-window halo so interior scores are tile-agnostic (determinism).
///
/// Scale: CovarianceMode::Full accumulates a B x B covariance per pixel —
/// O(window * B^2) per pixel. For hyperspectral cubes (>= ~256 bands) prefer
/// CovarianceMode::Diagonal ("RX-D" in the Manolakis taxonomy), which uses
/// per-band variances only — O(window * B) per pixel — at the cost of
/// ignoring inter-band correlation.
namespace SpectralLocalRx
{
    /// Local covariance estimator.
    enum class CovarianceMode
    {
        Full,     ///< local full covariance + scaled diagonal loading
        Diagonal, ///< local per-band variances only (high-band-count mode)
    };

    const char *covarianceModeText( CovarianceMode mode );
    bool covarianceModeFromText( const std::string &text, CovarianceMode *out );

    struct Config
    {
        int outerWindow = 5;  ///< odd side length >= 3 of the background window
        int innerWindow = 3;  ///< odd guard side length >= 1, < outerWindow
        CovarianceMode covarianceMode = CovarianceMode::Full;
        double loading = 1e-3; ///< alpha of the scaled diagonal loading (>= 0)
        int minBackgroundSamples = 0; ///< 0 = auto: Full -> 2*B + 2, Diagonal -> B + 1
    };

    /// Scores plus per-pixel quality planes (all size width*height).
    struct Result
    {
        std::vector<float> scores;          ///< RX score; NaN = unscored
        std::vector<uint8_t> scored;        ///< 1 when the pixel has a score
        std::vector<int32_t> backgroundSamples; ///< valid background count per pixel
        CovarianceMode covarianceMode = CovarianceMode::Full;
    };

    /**
     * Dual-window RX over a full raster tile.
     *
     * @param pixels  row-major spectra: pixels[(y*width + x) * bands + b]
     * @param noDataBands optional per-band NoData values (size bands);
     *        consulted only for bands whose hasNoDataBands entry is set
     * @param hasNoDataBands optional per-band flags (size bands)
     *
     * A pixel is valid when every band value is finite and — for bands with
     * a declared NoData — not equal to that NoData. Invalid pixels are never
     * scored and never enter any background set.
     *
     * @return false only for structurally invalid arguments (null buffers,
     *         non-positive extents/bands, bad window geometry, negative
     *         loading, inner >= outer). Per-window degeneracy (too few
     *         valid background samples, singular covariance after loading)
     *         is NOT an error: those pixels are reported unscored.
     */
    bool dualWindowRx( const float *pixels, int width, int height, int bands,
                       const Config &config,
                       const float *noDataBands,
                       const uint8_t *hasNoDataBands,
                       Result *result,
                       QString *errorMessage = nullptr );

    /**
     * Single-pixel score against an explicit background set (pixels not
     * including the center). Exposed for the streaming operator (tile-local
     * enumeration) and for independent verification; same validity and
     * loading rules as dualWindowRx. Returns false with an error message
     * when the background is structurally insufficient (count below the
     * minimum or a singular loaded covariance) — the caller decides whether
     * that means "leave unscored".
     */
    bool scorePixel( const float *spectrum,
                     const float *background, size_t backgroundCount, int bands,
                     const Config &config,
                     const float *noDataBands,
                     const uint8_t *hasNoDataBands,
                     float *score,
                     QString *errorMessage = nullptr );

} // namespace SpectralLocalRx
