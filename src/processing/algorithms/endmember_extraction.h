// src/processing/algorithms/endmember_extraction.h — endmember extraction
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

/// Endmember extraction kernels (Pixel Purity Index).
namespace EndmemberExtraction
{
    /// Extracted endmembers: spectra (endmember-major), the source pixel
    /// indices they came from, and the per-pixel PPI counts that ranked them.
    struct EndmemberResult
    {
        std::vector<float> endmembers;       // endmembers[e * bands + b]
        std::vector<int> endmemberIndices;   // source pixel index per endmember
        std::vector<int> ppiCounts;          // per-pixel extreme counts
    };

    /**
     * Pixel Purity Index (Boardman et al., 1995): project all pixels onto
     * @p projections random unit vectors (seeded, reproducible) and count how
     * often each pixel is the projection extreme; the @p nEndmembers pixels
     * with the highest counts are the extracted endmembers.
     *
     * @param pixels      pixel-major spectra: pixels[p * bands + b]
     * @param count       number of pixels
     * @param bands       spectral bands per pixel
     * @param nEndmembers number of endmembers to extract (>= 1, <= count)
     * @param projections number of random projections (>= 16)
     * @param result      [out] endmembers + indices + counts
     * @param errorMessage optional error sink
     * @return true on success; false for invalid arguments or zero-variance data
     */
    bool pixelPurityIndex( const float *pixels, size_t count, int bands,
                           int nEndmembers, int projections,
                           EndmemberResult *result, QString *errorMessage = nullptr );
} // namespace EndmemberExtraction
