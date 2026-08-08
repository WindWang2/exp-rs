// src/processing/algorithms/spectral_anomaly.h — hyperspectral anomaly detection
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

/// Hyperspectral anomaly detection kernels.
namespace SpectralAnomaly
{
    /// Background statistics for the RX detector, computed in streaming passes.
    /// mean.size() == bands; covariance.size() == bands*bands (row-major). The
    /// covariance is the biased sample covariance (sum of centered outer
    /// products / count) — bit-identical to rxDetector's two-pass computation.
    /// Exposed so streaming operators can compute stats in tile passes and then
    /// score per-tile without materializing the whole raster (perf goal §2c).
    /// Note: the mean and covariance passes are SEPARATE accumulators (a mean
    /// pass must be finalized before a covariance pass begins) to preserve the
    /// exact computation order of rxDetector.
    struct BackgroundStats
    {
        std::vector<double> mean;
        std::vector<double> covariance;
        size_t count = 0; ///< number of pixels accumulated in the current pass
    };

    /// Accumulate a sum-of-values pass (for the mean). Resets @a stats for a mean
    /// pass on first call (band-count inferred). @a pixels layout: pixels[p*bands+b].
    void accumulateMean( const float *pixels, size_t count, int bands,
                         BackgroundStats *stats );

    /// Divide the accumulated sum by count → mean. Resets the count/sum state.
    void finalizeMean( BackgroundStats *stats );

    /// Accumulate a centered-sum-of-outer-products pass (for the covariance),
    /// using the already-finalized mean. Must be called AFTER finalizeMean().
    void accumulateCovariance( const float *pixels, size_t count, int bands,
                               BackgroundStats *stats );

    /// Divide the accumulated covariance sum by count → biased covariance.
    void finalizeCovariance( BackgroundStats *stats );

    /// Invert the (ridged) covariance. Adds a small ridge (1e-9) on the diagonal
    /// for robustness. Returns false when singular.
    bool invertCovariance( const std::vector<double> &covariance, int bands,
                           std::vector<double> *inverse );

    /// Per-pixel RX score for one spectrum given precomputed mean + inverse cov.
    /// Equivalent to the per-pixel step of rxDetector. @a spectrum size == bands.
    float rxScore( const float *spectrum, const std::vector<double> &mean,
                   const std::vector<double> &inverseCov, int bands );

    /**
     * Reed-Xiaoli (RX) detector: per-pixel Mahalanobis distance to the global
     * background statistics.
     *
     *   RX(x) = (x - mu)^T Sigma^-1 (x - mu)
     *
     * where mu and Sigma are the sample mean / covariance of all pixels. The
     * covariance is inverted with a small ridge (1e-9 on the diagonal) for
     * robustness; high values indicate anomalous pixels.
     *
     * @param pixels     pixel-major spectra: pixels[p * bands + b]
     * @param count      number of pixels
     * @param bands      spectral bands per pixel
     * @param rxValues   [out] per-pixel RX score (count floats)
     * @param errorMessage optional error sink
     * @return true on success; false for invalid arguments (count == 0,
     *         bands <= 0, null pointers) or a singular covariance
     */
    bool rxDetector( const float *pixels, size_t count, int bands,
                     std::vector<float> *rxValues, QString *errorMessage = nullptr );
} // namespace SpectralAnomaly
