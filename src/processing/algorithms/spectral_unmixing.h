// src/processing/algorithms/spectral_unmixing.h — linear spectral unmixing
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

/// Linear spectral unmixing: per-pixel abundance estimation of a set of
/// endmember spectra.
///
/// Method: per pixel, solve the least-squares system E a = x (E = endmember
/// matrix, band-major columns) via normal equations with a small ridge on the
/// diagonal; clip abundances to [0, 1] and renormalize to unit sum. The
/// reconstruction error is the per-pixel RMSE ||x - E a|| / sqrt(bands).
namespace SpectralUnmixing
{
    /// Abundances are pixel-major: abundances[p * nEndmembers + e].
    struct UnmixResult
    {
        std::vector<float> abundances;
        std::vector<float> reconstructionError; // per-pixel RMSE (float)
    };

    /**
     * Unmix @p count pixels against @p nEndmembers endmember spectra.
     *
     * @param pixels      pixel-major spectra: pixels[p * bands + b]
     * @param count       number of pixels
     * @param bands       spectral bands per pixel / endmember
     * @param endmembers  endmember-major: endmembers[e * bands + b]
     * @param nEndmembers number of endmembers (must be >= 1 and <= bands)
     * @param result      [out] abundances + per-pixel reconstruction error
     * @param errorMessage optional error sink
     * @return true on success; false for invalid arguments (count == 0,
     *         bands <= 0, nEndmembers out of [1, bands], null pointers)
     */
    bool unmix( const float *pixels, size_t count, int bands,
                const float *endmembers, int nEndmembers,
                UnmixResult *result, QString *errorMessage = nullptr );
} // namespace SpectralUnmixing
