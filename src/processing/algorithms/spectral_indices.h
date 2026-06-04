// src/processing/algorithms/spectral_indices.h
#pragma once

#include <cstddef>

/**
 * Spectral index calculation functions.
 *
 * All functions operate on raw float arrays (band data).
 * Output arrays must be pre-allocated with the same size as input.
 * Returns true on success, false on invalid arguments.
 *
 * Convention: NaN is used for undefined values (e.g., 0/0).
 */
namespace SpectralIndices
{
    /**
     * NDVI = (NIR - Red) / (NIR + Red)
     * @param nir   NIR band data
     * @param red   Red band data
     * @param out   output buffer
     * @param count number of pixels
     */
    bool ndvi(const float *nir, const float *red, float *out, size_t count);

    /**
     * EVI = 2.5 * (NIR - Red) / (NIR + 6*Red - 7.5*Blue + 1)
     */
    bool evi(const float *nir, const float *red, const float *blue, float *out, size_t count);

    /**
     * SAVI = (NIR - Red) / (NIR + Red + L) * (1 + L), where L=0.5
     */
    bool savi(const float *nir, const float *red, float *out, size_t count);

    /**
     * NDWI = (Green - NIR) / (Green + NIR)
     */
    bool ndwi(const float *green, const float *nir, float *out, size_t count);

    /**
     * NDBI = (SWIR - NIR) / (SWIR + NIR)
     */
    bool ndbi(const float *swir, const float *nir, float *out, size_t count);

    /**
     * MNDWI = (Green - SWIR) / (Green + SWIR)
     */
    bool mndwi(const float *green, const float *swir, float *out, size_t count);
}
