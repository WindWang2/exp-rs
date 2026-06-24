// src/processing/algorithms/spectral_indices.cpp — Spectral index implementations
#include "spectral_indices.h"
#include "math_utils.h"
#include "core/sicnu_logging.h"
#include "framework/input_validator.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace SpectralIndices
{

bool ndvi(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "ndvi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    SICNU_LOG_DEBUG( SicnuLogTags::Algorithms, QString( "Computing NDVI: %1 pixels" ).arg( count ) );
    return MathUtils::normalizedDifference(nir, red, out, count);
}

bool evi(const float *nir, const float *red, const float *blue, float *out, size_t count)
{
    if (!nir || !red || !blue || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "evi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    for (size_t i = 0; i < count; i++) {
        float denom = nir[i] + 6.0f * red[i] - 7.5f * blue[i] + 1.0f;
        out[i] = MathUtils::safeDiv(2.5f * (nir[i] - red[i]), denom);
    }
    return true;
}

bool savi(const float *nir, const float *red, float *out, size_t count)
{
    if (!nir || !red || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "savi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    constexpr float L = 0.5f;
    for (size_t i = 0; i < count; i++) {
        out[i] = MathUtils::safeDiv(nir[i] - red[i], nir[i] + red[i] + L) * (1.0f + L);
    }
    return true;
}

bool ndwi(const float *green, const float *nir, float *out, size_t count)
{
    if (!green || !nir || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "ndwi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    return MathUtils::normalizedDifference(green, nir, out, count);
}

bool ndbi(const float *swir, const float *nir, float *out, size_t count)
{
    if (!swir || !nir || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "ndbi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    return MathUtils::normalizedDifference(swir, nir, out, count);
}

bool mndwi(const float *green, const float *swir, float *out, size_t count)
{
    if (!green || !swir || !out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "mndwi: null pointer argument");
        return false;
    }
    if (count == 0) return false;
    return MathUtils::normalizedDifference(green, swir, out, count);
}

} // namespace SpectralIndices
