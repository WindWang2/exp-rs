// src/processing/algorithms/spectral_anomaly.h — hyperspectral anomaly detection
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

/// Hyperspectral anomaly detection kernels.
namespace SpectralAnomaly
{
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
